#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "diskio.h"
#include "smfsconfig.h"
#include "smfs.h"

uint8_t __BUFFER[BLOCK_SIZE];

uint32_t __TOTAL_FILES 				 = 0;
uint32_t __NEXT_FREE_CLUSTER 		 = FILE_START_CLUSTER;
uint32_t __NEXT_FREE_INFO_BLOCK 	 = 1;
uint32_t __NEXT_OVERWRITABLE_CLUSTER = 0;
uint64_t __DATA_SPACE_USED			 = 0;
uint64_t __ACTUAL_SPACE_USED		 = 0;

static inline uint32_t __create_32bit_number(uint8_t* _startByte)
{
	uint32_t number = ((*_startByte << 24) | (*(_startByte+1) << 16) | (*(_startByte+2) << 8) | *(_startByte+3));

	return number;
}

static inline void __create_8bit_number(uint32_t _number, uint8_t* _buffer)
{
	*(_buffer + 0) = (_number >> 24);
	*(_buffer + 1) = (_number >> 16);
	*(_buffer + 2) = (_number >> 8);
	*(_buffer + 3) = (_number);
}

static inline void clear_buffer(void)
{
	memset(__BUFFER, '\0', sizeof(__BUFFER));
}

static inline void __readBlock0(void)
{
	disk_readBlock(0, __BUFFER);
}

static inline void __writeBlock0(void)
{
	disk_writeBlock(0, __BUFFER);
}

static uint32_t __getFreeCluster(CLUSTER_TYPE* _cluster_type)
{
	__readBlock0();

	uint32_t next_free_cluster    = __create_32bit_number(&__BUFFER[NEXT_FREE_CLUSTER_INDEX]);
	uint32_t overwritable_cluster = __create_32bit_number(&__BUFFER[NEXT_OVERWRITABLE_CLUSTER_INDEX]);

	if(overwritable_cluster != 0)		//If Overwritable Cluster Is Available
	{
		uint8_t buffer[BLOCK_SIZE];

		disk_readBlock(overwritable_cluster + (CLUSTER - 1), buffer);
		
		uint32_t next_overwritable_cluster = __create_32bit_number(&buffer[NEXT_CLUSTER_LINK_INDEX]);

		__create_8bit_number(next_overwritable_cluster, &__BUFFER[NEXT_OVERWRITABLE_CLUSTER_INDEX]);

		if(next_overwritable_cluster == 0)
		{
			__create_8bit_number(next_overwritable_cluster, &__BUFFER[LAST_OVERWRITABLE_CLUSTER_INDEX]);
		}
		__writeBlock0();

		clear_buffer();

		*_cluster_type = OVERWRITABLE_CLUSTER;

		return overwritable_cluster;
	}
	else
	{
		__NEXT_FREE_CLUSTER += CLUSTER;
		__create_8bit_number(__NEXT_FREE_CLUSTER, &__BUFFER[NEXT_FREE_CLUSTER_INDEX]);
		__writeBlock0();
		
		clear_buffer();

		*_cluster_type = FREE_CLUSTER;

		return next_free_cluster;
	}
}

static uint32_t __getFreeInfoBlock(BLOCK_TYPE* _block_type)
{
	uint8_t file_info_buffer[BLOCK_SIZE];

	__readBlock0();

	__NEXT_FREE_INFO_BLOCK = __create_32bit_number(&__BUFFER[NEXT_FREE_INFO_BLOCK_INDEX]);
	uint32_t next_block = __create_32bit_number(&__BUFFER[NEXT_DELETED_BLOCK_INDEX]);

	if(next_block != 0)
	{
		uint32_t current_block = next_block;

		disk_readBlock(next_block, file_info_buffer);
		next_block = __create_32bit_number(&file_info_buffer[DELETED_INFO_BLOCK_INDEX]);
		
		__create_8bit_number(next_block, &__BUFFER[NEXT_DELETED_BLOCK_INDEX]);
		
		if(next_block == 0)
		{
			__create_8bit_number(0, &__BUFFER[LAST_DELETED_BLOCK_INDEX]);
		}
		__writeBlock0();

		clear_buffer();

		*_block_type = DELETED_FILE_BLOCK;

		return current_block;
	}
	else
	{
		clear_buffer();

		*_block_type = FREE_BLOCK;
	
		return __NEXT_FREE_INFO_BLOCK;
	}
}

static uint32_t __getLastCluster(uint32_t _start_cluster)
{
	uint8_t buffer[BLOCK_SIZE];

	uint32_t current_cluster = _start_cluster;

	while(1)
	{
		disk_readBlock((current_cluster + (CLUSTER - 1)), buffer);
		uint32_t next_cluster = __create_32bit_number(&buffer[NEXT_CLUSTER_LINK_INDEX]);
		if(next_cluster == 0) return current_cluster;
		current_cluster = next_cluster;
	}
}

static void __connect_to_overwritable_chain(uint32_t _cluster)
{
	uint8_t buffer[BLOCK_SIZE];

	__readBlock0();

	uint32_t next_cluster    = __create_32bit_number(&__BUFFER[NEXT_OVERWRITABLE_CLUSTER_INDEX]);
	uint32_t last_cluster    = __create_32bit_number(&__BUFFER[LAST_OVERWRITABLE_CLUSTER_INDEX]);

	if(next_cluster != 0)
	{
		disk_readBlock((last_cluster + (CLUSTER - 1)), buffer);
		__create_8bit_number(_cluster, &buffer[NEXT_CLUSTER_LINK_INDEX]);
		disk_writeBlock((last_cluster + (CLUSTER - 1)), buffer);

		last_cluster = __getLastCluster(_cluster);

		__create_8bit_number(last_cluster, &__BUFFER[LAST_OVERWRITABLE_CLUSTER_INDEX]);

		__writeBlock0();
	}
	else
	{
		__create_8bit_number(_cluster, &__BUFFER[NEXT_OVERWRITABLE_CLUSTER_INDEX]);
		__create_8bit_number(_cluster, &__BUFFER[LAST_OVERWRITABLE_CLUSTER_INDEX]);
		__writeBlock0();
	}
	clear_buffer();
}

static void __create_file_info_block(SMFS_FILE* _file, const char* _name)
{
	BLOCK_TYPE block_type;
	CLUSTER_TYPE cluster_type;
	
	uint32_t start_cluster = __getFreeCluster(&cluster_type);

	clear_buffer();

	__readBlock0();

	uint32_t info_block = __getFreeInfoBlock(&block_type);

	if(block_type == DELETED_FILE_BLOCK)
	{
		uint8_t buffer[BLOCK_SIZE];

		disk_readBlock(info_block, buffer);
		uint32_t file_start_cluster = __create_32bit_number(&buffer[NEXT_CLUSTER_LINK_INDEX]);
		uint32_t next_deleted_file_block = __create_32bit_number(&buffer[DELETED_INFO_BLOCK_INDEX]);
		__connect_to_overwritable_chain(file_start_cluster);
		uint32_t last_cluster = __getLastCluster(file_start_cluster);
		__create_8bit_number(last_cluster, &__BUFFER[LAST_OVERWRITABLE_CLUSTER_INDEX]);
	}
	strcpy(_file->name, _name);
	_file->start_cluster = start_cluster;
	_file->info_block 	 = info_block;
	_file->size		  	 = 0;
	_file->size_on_disk  = 0;
	_file->status		 = FILE_STATUS_NEW_FILE;

	strcpy(__BUFFER, _file->name);
	__BUFFER[FILE_STATUS_INDEX] = _file->status;
	__create_8bit_number(_file->start_cluster, &__BUFFER[FILE_START_CLUSTER_INDEX]);
	__create_8bit_number(_file->size, &__BUFFER[FILE_SIZE_INDEX]);
}

void smfs_format_disk(void)
{
	clear_buffer();

	/*FILES START CLUSTER*/
	__create_8bit_number(FILE_START_CLUSTER, 	 &__BUFFER[NEXT_FREE_CLUSTER_INDEX]);
	/*FILES INFO START CLUSTER*/
	__create_8bit_number(FILES_INFO_START_BLOCK, &__BUFFER[NEXT_FREE_INFO_BLOCK_INDEX]);
	/*NEXT OVERWRITABLE CLUSTER = 0*/
	__create_8bit_number(0, 					 &__BUFFER[NEXT_OVERWRITABLE_CLUSTER_INDEX]);
	/*LAST OVERWRITABLE CLUSTER = 0*/
	__create_8bit_number(0, 					 &__BUFFER[LAST_OVERWRITABLE_CLUSTER_INDEX]);
	/*TOTAL FILES = 0*/
	__create_8bit_number(0, 					 &__BUFFER[TOTAL_FILES_INDEX]);
	/*DATA SPACE USED*/
	__create_8bit_number(0, 	 				 &__BUFFER[TOTAL_SPACE_USED_INDEX]);
	/*ACTUAL SPACE USED*/
	__create_8bit_number(0, 	 				 &__BUFFER[ACTUAL_SPACE_USED_INDEX]);
	/*FILE SYSTEM STATUS = OK*/
	__create_8bit_number(FS_OK, 				 &__BUFFER[FILESYSTEM_STATUS_INDEX]);

	__writeBlock0();

	clear_buffer();
}

SMFS_STATUS smfs_init(void)
{
	disk_init();

	clear_buffer();

	__readBlock0();

	__NEXT_FREE_CLUSTER 		= __create_32bit_number(&__BUFFER[NEXT_FREE_CLUSTER_INDEX]);
	__NEXT_FREE_INFO_BLOCK 		= __create_32bit_number(&__BUFFER[NEXT_FREE_INFO_BLOCK_INDEX]);
	__NEXT_OVERWRITABLE_CLUSTER = __create_32bit_number(&__BUFFER[NEXT_OVERWRITABLE_CLUSTER_INDEX]);
	__DATA_SPACE_USED			= __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);
	__ACTUAL_SPACE_USED			= __create_32bit_number(&__BUFFER[ACTUAL_SPACE_USED_INDEX]);

	SMFS_STATUS status = __create_32bit_number(&__BUFFER[FILESYSTEM_STATUS_INDEX]);

	clear_buffer();

	if(status == FS_OK)
	{
		return FS_OK;
	}
	if(status != FS_MAX_FILES_REACHED)
	{
		status = FS_NO_FILESYSTEM;
	}
	return status;
}

uint64_t smfs_getTotalStorageUsed(void)
{
	__readBlock0();

	__DATA_SPACE_USED = __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);

	uint64_t cluster_space = ((__DATA_SPACE_USED * CLUSTER) * BLOCK_SIZE);
	uint64_t total_space_used = (SYSTEM_SPACE_USED + cluster_space); 

	clear_buffer();

	return total_space_used;
}

uint64_t smfs_getDataStorageUsed(void)
{
	__readBlock0();

	__DATA_SPACE_USED = __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);

	uint64_t cluster_space = ((__DATA_SPACE_USED * CLUSTER) * BLOCK_SIZE);

	clear_buffer();

	return cluster_space;
}

SMFS_FCREATE_RESULT smfs_create(SMFS_FILE* _file, const char* _name)
{
	__readBlock0();

	__TOTAL_FILES 	  	= __create_32bit_number(&__BUFFER[TOTAL_FILES_INDEX]);
	__DATA_SPACE_USED 	= __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);
	__ACTUAL_SPACE_USED = __create_32bit_number(&__BUFFER[ACTUAL_SPACE_USED_INDEX]);

	if(__TOTAL_FILES < MAX_NUMBER_OF_FILES)
	{
		__create_file_info_block(_file, _name);

		disk_writeBlock(_file->info_block, __BUFFER);

		clear_buffer();

		__readBlock0();

		__TOTAL_FILES++;
		__NEXT_FREE_INFO_BLOCK++;
		__DATA_SPACE_USED++;
		__ACTUAL_SPACE_USED++;

		__create_8bit_number(__NEXT_FREE_INFO_BLOCK, &__BUFFER[NEXT_FREE_INFO_BLOCK_INDEX]);
		__create_8bit_number(__TOTAL_FILES, 	 	 &__BUFFER[TOTAL_FILES_INDEX]);
		__create_8bit_number(__DATA_SPACE_USED, 	 &__BUFFER[TOTAL_SPACE_USED_INDEX]);
		__create_8bit_number(__ACTUAL_SPACE_USED, 	 &__BUFFER[ACTUAL_SPACE_USED_INDEX]);

		__writeBlock0();

		clear_buffer();

		return FILE_CREATE_SUCCESS;
	}
	return FILE_CREATE_FAILURE;
}

static void __readFileInfoBlock(const char* _name)	//To Be Used As A Debugging Function Only
{
	clear_buffer();

	int i = 0;
	uint8_t file_found = 0;

	printf("\nSEARCHING FILE: %s\n", _name);

	for(; i<MAX_NUMBER_OF_FILES; i++)
	{
		disk_readBlock((FILES_INFO_START_BLOCK + i), __BUFFER);

		char buff[FILENAME_MAX_LENGTH];

		strcpy(buff, (const char*)__BUFFER);

		if(strcmp(buff, _name) == 0)
		{
			file_found = 1;
			break;
		}
	}
	if(file_found == 0) printf("FILE NOT FOUND...\n");
	
	else
	{
		uint32_t file_start_block = __create_32bit_number(&__BUFFER[FILE_START_CLUSTER_INDEX]);

		printf("\nFILE FOUND...\n\n");
		printf("FILE INFO BLOCK: %d\n", FILES_INFO_START_BLOCK + i);
		printf("FILE NAME: %s\n", __BUFFER);
		printf("FILE START BLOCK: %d\n", file_start_block);
	}
}

uint32_t smfs_getFileSize(SMFS_FILE* _file)
{
	clear_buffer();
	disk_readBlock(_file->info_block, __BUFFER);
	uint32_t size = __create_32bit_number(&__BUFFER[FILE_SIZE_INDEX]);
	_file->size = size;
	clear_buffer();
	return size;
}

SMFS_FOPEN_RESULT smfs_open(SMFS_FILE* _file, const char* _name, SMFS_FOPEN_MODE _open_mode)
{
	char buff[FILENAME_MAX_LENGTH];

	clear_buffer();

	int i = 0;
	uint8_t file_found = 0;
	
	for(; i<MAX_NUMBER_OF_FILES; i++)
	{
		disk_readBlock((FILES_INFO_START_BLOCK + i), __BUFFER);
		strcpy(buff, (const char*)__BUFFER);
		uint8_t file_status = __BUFFER[FILE_STATUS_INDEX];

		if((strcmp(buff, _name) == 0) && ((file_status & FILE_STATUS_DELETED) != FILE_STATUS_DELETED))
		{
			file_found = 1;
			break;
		}
	}
	if(file_found == 0)
	{
		return FILE_NOT_FOUND;
	}
	else
	{
		_file->start_cluster = __create_32bit_number(&__BUFFER[FILE_START_CLUSTER_INDEX]);
		_file->size			 = __create_32bit_number(&__BUFFER[FILE_SIZE_INDEX]);
		_file->info_block    = FILES_INFO_START_BLOCK + i;
		_file->status 		 = __BUFFER[FILE_STATUS_INDEX];
		_file->open_mode	 = _open_mode;

		float clusters_occupied = (float)_file->size / (float)(BLOCK_SIZE * CLUSTER);
		if(clusters_occupied - (int)clusters_occupied != 0) clusters_occupied++;
		_file->size_on_disk = ((int)clusters_occupied * (CLUSTER * BLOCK_SIZE));
		
		strcpy(_file->name, __BUFFER);
		
		return FILE_OPEN_SUCCESS;
	}
}

SMFS_FWRITE_RESULT smfs_write(SMFS_FILE* _file, const char* _data)
{
	uint64_t data_len = strlen(_data);
	uint64_t space_used = smfs_getTotalStorageUsed();
	uint64_t space_remaining = space_used - MAX_DATA_SPACE;
	
	__readBlock0();

	__TOTAL_FILES = __create_32bit_number(&__BUFFER[TOTAL_FILES_INDEX]);

	uint32_t deleted_files = __create_32bit_number(&__BUFFER[DELETED_FILES_INDEX]);

	if(data_len > space_remaining) return FILE_WRITE_FAILURE_NOT_ENOUGH_MEMORY;
	else if(space_used == MAX_DATA_SPACE) return FILE_WRITE_FAILURE_MEMORY_FULL;
	else if(data_len > 0xFFFFFFFF) return FILE_WRITE_FAILURE_DATA_TOO_LARGE;
	else if(__TOTAL_FILES == MAX_NUMBER_OF_FILES) return FILE_WRITE_FAILURE_MAX_FILES_REACHED;
	else if(_file->open_mode == FILE_OPEN_MODE_READ) return FILE_WRITE_FAILURE_FILE_MODE_READONLY;

	clear_buffer();

	if(_file->open_mode == FILE_OPEN_MODE_WRITE)
	{
		disk_readBlock(_file->info_block, __BUFFER);
		_file->status = __BUFFER[FILE_STATUS_INDEX];
		
		if((_file->status != FILE_STATUS_DELETED) || (_file->status != FILE_STATUS_MAX_SIZE) || (_file->status != FILE_STATUS_READONLY))
		{
			uint32_t start_index   = 0;
			uint32_t previous_size = 0;
			uint32_t bytes_written = 0;
			uint32_t current_size  = data_len;

			_file->size = smfs_getFileSize(_file);
			previous_size = _file->size;

			float prev_clusters_needed = (float)previous_size / (float)(BLOCK_SIZE * CLUSTER);
			if(prev_clusters_needed - (int)prev_clusters_needed != 0) prev_clusters_needed++;

			float current_clusters_needed = (float)data_len / (float)(BLOCK_SIZE * CLUSTER);
			if(current_clusters_needed - (int)current_clusters_needed != 0) current_clusters_needed++;

			if(_file->size != current_size) 
			{
				disk_readBlock(_file->info_block, __BUFFER);
				__create_8bit_number(current_size, &__BUFFER[FILE_SIZE_INDEX]);
				disk_writeBlock(_file->info_block, __BUFFER);

				clear_buffer();
			}

			if(_file->status == FILE_STATUS_NEW_FILE)	//If The File Is New With No Data Written Previously
			{
				if((int)current_clusters_needed > 1)
				{
					__readBlock0();
					uint32_t space_used = __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);
					uint32_t actual_space_used = __create_32bit_number(&__BUFFER[ACTUAL_SPACE_USED_INDEX]);
					space_used += ((int)current_clusters_needed - 1);		//Update Total Space Used
					actual_space_used += ((int)current_clusters_needed - 1);		//Update Total Space Used
					__create_8bit_number(space_used, &__BUFFER[TOTAL_SPACE_USED_INDEX]);
					__create_8bit_number(actual_space_used, &__BUFFER[ACTUAL_SPACE_USED_INDEX]);
					__writeBlock0();
				}

				uint32_t next_cluster  = _file->start_cluster;

				while((int)current_clusters_needed)		//To Write Clusters
				{
					for(int i=0; i<CLUSTER; i++)		//To Write Blocks Of A Cluster
					{
						if(i != (CLUSTER - 1))			//First CLUSTER - 1 Blocks Of The Cluster
						{
							strncpy(__BUFFER, _data + start_index, BLOCK_SIZE);

							disk_writeBlock((next_cluster + i), __BUFFER);

							start_index   += BLOCK_SIZE;
							bytes_written += BLOCK_SIZE;

							if(bytes_written >= data_len) break;
						}
						else							//Last Block Of The Cluster
						{
							strncpy(__BUFFER, _data + start_index, BLOCK_SIZE - 4);

							disk_writeBlock((next_cluster + i), __BUFFER);

							start_index   += BLOCK_SIZE - 4;
							bytes_written += BLOCK_SIZE - 4;

							if(bytes_written >= data_len) break;
						}
					}
					if((int)current_clusters_needed != 1)
					{
						CLUSTER_TYPE type;

						uint32_t cluster_to_link_to = next_cluster;

						next_cluster = __getFreeCluster(&type);

						disk_readBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
						__create_8bit_number(next_cluster, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
						disk_writeBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
					}
					else
					{
						__create_8bit_number(0, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
						disk_writeBlock((next_cluster + (CLUSTER - 1)), __BUFFER);
					}
					current_clusters_needed--;
				}
				_file->status = FILE_STATUS_PREVOUSLY_WRITTEN;

				disk_readBlock(_file->info_block, __BUFFER);
				__BUFFER[FILE_STATUS_INDEX] = _file->status;
				disk_writeBlock(_file->info_block, __BUFFER);

				return FILE_WRITE_SUCCESS;
			}
			else										//If File Is Old With Data Written Previously
			{
				float new_clusters_needed = (float)data_len / (float)(BLOCK_SIZE * CLUSTER);
				if(new_clusters_needed - (int)new_clusters_needed != 0) new_clusters_needed++;

				if((int)new_clusters_needed < (int)prev_clusters_needed)
				{
					uint32_t last_cluster = _file->start_cluster;
					
					__readBlock0();
					uint32_t space_used = __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);
					uint32_t actual_space_used = __create_32bit_number(&__BUFFER[ACTUAL_SPACE_USED_INDEX]);
					uint32_t space_freed = (int)prev_clusters_needed - (int)new_clusters_needed;
					space_used -= space_freed;
					actual_space_used -= space_freed;
					__create_8bit_number(space_used, &__BUFFER[TOTAL_SPACE_USED_INDEX]);
					__create_8bit_number(actual_space_used, &__BUFFER[ACTUAL_SPACE_USED_INDEX]);
					__writeBlock0();

					if((int)prev_clusters_needed != 1)
					{
						uint32_t clusters_needed = (int)new_clusters_needed;

						while(clusters_needed)			//Traverse And Find The Last Cluster
						{
							disk_readBlock((last_cluster + (CLUSTER - 1)), __BUFFER);
							uint32_t connected_cluster = __create_32bit_number(&__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
							last_cluster = connected_cluster;
							clusters_needed--;
						}
						__connect_to_overwritable_chain(last_cluster);
					}
					uint32_t next_cluster = _file->start_cluster;
					uint32_t connected_cluster = 0;

					while((int)new_clusters_needed)			//To Write Clusters
					{
						for(int i=0; i<CLUSTER; i++)		//To Write Blocks Of A Cluster
						{
							if(i != (CLUSTER - 1))			//First CLUSTER - 1 Blocks Of The Cluster
							{
								strncpy(__BUFFER, _data + start_index, BLOCK_SIZE);

								disk_writeBlock((next_cluster + i), __BUFFER);

								start_index   += BLOCK_SIZE;
								bytes_written += BLOCK_SIZE;

								if(bytes_written >= data_len) break;
							}
							else							//Last Block Of The Cluster
							{
								disk_readBlock((next_cluster + i), __BUFFER);

								connected_cluster = __create_32bit_number(&__BUFFER[NEXT_CLUSTER_LINK_INDEX]);

								strncpy(__BUFFER, _data + start_index, BLOCK_SIZE - 4);

								disk_writeBlock((next_cluster + i), __BUFFER);

								start_index   += BLOCK_SIZE - 4;
								bytes_written += BLOCK_SIZE - 4;

								if(bytes_written >= data_len) break;
							}
						}
						if((int)new_clusters_needed != 1)
						{
							next_cluster = connected_cluster;
						}
						else
						{
							disk_readBlock((next_cluster + (CLUSTER - 1)), __BUFFER);
							__create_8bit_number(0, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
							disk_writeBlock((next_cluster + (CLUSTER - 1)), __BUFFER);
						}
						new_clusters_needed--;
					}
					return FILE_WRITE_SUCCESS;
				}
				else if((int)new_clusters_needed == (int)prev_clusters_needed)
				{
					uint32_t next_cluster = _file->start_cluster;
					uint32_t connected_cluster = 0;

					while((int)new_clusters_needed)			//To Write Clusters
					{
						for(int i=0; i<CLUSTER; i++)		//To Write Blocks Of A Cluster
						{
							if(i != (CLUSTER - 1))			//First CLUSTER - 1 Blocks Of The Cluster
							{
								strncpy(__BUFFER, _data + start_index, BLOCK_SIZE);

								disk_writeBlock((next_cluster + i), __BUFFER);

								start_index   += BLOCK_SIZE;
								bytes_written += BLOCK_SIZE;

								if(bytes_written >= data_len) break;
							}
							else							//Last Block Of The Cluster
							{
								disk_readBlock((next_cluster + i), __BUFFER);

								connected_cluster = __create_32bit_number(&__BUFFER[NEXT_CLUSTER_LINK_INDEX]);

								strncpy(__BUFFER, _data + start_index, BLOCK_SIZE - 4);
								
								__create_8bit_number(connected_cluster, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);

								disk_writeBlock((next_cluster + i), __BUFFER);

								next_cluster = connected_cluster;

								start_index   += BLOCK_SIZE - 4;
								bytes_written += BLOCK_SIZE - 4;

								if(bytes_written >= data_len) break;
							}
						}
						new_clusters_needed--;
					}
					return FILE_WRITE_SUCCESS;
				}
				else
				{
					uint32_t next_cluster = _file->start_cluster;
					uint32_t connected_cluster = 0;
					uint32_t clusters_written  = 0;

					__readBlock0();
					uint32_t space_used = __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);
					uint32_t actual_space_used = __create_32bit_number(&__BUFFER[ACTUAL_SPACE_USED_INDEX]);
					uint32_t space_increased = (int)current_clusters_needed - (int)prev_clusters_needed;
					space_used += space_increased;				//Update Total Space Used
					__create_8bit_number(space_used, &__BUFFER[TOTAL_SPACE_USED_INDEX]);
					__create_8bit_number(actual_space_used, &__BUFFER[ACTUAL_SPACE_USED_INDEX]);
					__writeBlock0();

					while((int)current_clusters_needed)			//To Write Clusters
					{
						if(clusters_written < (int)prev_clusters_needed)
						{
							for(int i=0; i<CLUSTER; i++)		//To Write Blocks Of A Cluster
							{
								if(i != (CLUSTER - 1))			//First CLUSTER - 1 Blocks Of The Cluster
								{
									strncpy(__BUFFER, _data + start_index, BLOCK_SIZE);

									disk_writeBlock((next_cluster + i), __BUFFER);

									start_index   += BLOCK_SIZE;
									bytes_written += BLOCK_SIZE;

									if(bytes_written >= data_len) break;
								}
								else							//Last Block Of The Cluster
								{
									disk_readBlock((next_cluster + i), __BUFFER);

									connected_cluster = __create_32bit_number(&__BUFFER[NEXT_CLUSTER_LINK_INDEX]);

									strncpy(__BUFFER, _data + start_index, BLOCK_SIZE - 4);

									disk_writeBlock((next_cluster + i), __BUFFER);

									start_index   += BLOCK_SIZE - 4;
									bytes_written += BLOCK_SIZE - 4;

									if(bytes_written >= data_len) break;
								}
							}
							if(connected_cluster != 0)
							{
								next_cluster = connected_cluster;
							}
							else
							{
								uint32_t cluster_to_link_to = next_cluster;

								CLUSTER_TYPE cluster_type;

								next_cluster = __getFreeCluster(&cluster_type);

								disk_readBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
								__create_8bit_number(next_cluster, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
								disk_writeBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
							}
							clusters_written++;
							current_clusters_needed--;
						}
						else
						{
							for(int i=0; i<CLUSTER; i++)		//To Write Blocks Of A Cluster
							{
								if(i != (CLUSTER - 1))			//First CLUSTER - 1 Blocks Of The Cluster
								{
									strncpy(__BUFFER, _data + start_index, BLOCK_SIZE);

									disk_writeBlock((next_cluster + i), __BUFFER);

									start_index   += BLOCK_SIZE;
									bytes_written += BLOCK_SIZE;

									if(bytes_written >= data_len) break;
								}
								else							//Last Block Of The Cluster
								{
									strncpy(__BUFFER, _data + start_index, BLOCK_SIZE - 4);

									disk_writeBlock((next_cluster + i), __BUFFER);

									start_index   += BLOCK_SIZE - 4;
									bytes_written += BLOCK_SIZE - 4;

									if(bytes_written >= data_len) break;
								}
							}
							if((int)current_clusters_needed != 1)
							{
								CLUSTER_TYPE cluster_type;

								uint32_t cluster_to_link_to = next_cluster;

								next_cluster = __getFreeCluster(&cluster_type);

								disk_readBlock((cluster_to_link_to + (CLUSTER - 1)),  __BUFFER);
								__create_8bit_number(next_cluster, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
								disk_writeBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
							}
							else
							{
								__create_8bit_number(0, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
								disk_writeBlock((next_cluster + (CLUSTER - 1)), __BUFFER);
							}
							current_clusters_needed--;
						}
					}
					return FILE_WRITE_SUCCESS;
				}
			}
		}
	}
}

SMFS_FAPPEND_RESULT smfs_append(SMFS_FILE* _file, const char* _data)
{
	uint64_t data_len = strlen(_data);
	uint64_t space_used = smfs_getTotalStorageUsed();
	uint64_t space_remaining = space_used - MAX_DATA_SPACE;

	disk_readBlock(_file->info_block, __BUFFER);
	_file->status = __BUFFER[FILE_STATUS_INDEX];
	
	uint32_t start_index   = 0;
	uint32_t previous_size = __create_32bit_number(&__BUFFER[FILE_SIZE_INDEX]);
	uint32_t bytes_written = 0;
	uint32_t current_size  = data_len;
	uint32_t total_size    = previous_size + current_size;

	if(total_size > space_remaining) return FILE_APPEND_FAILURE_NOT_ENOUGH_MEMORY;
	else if(space_used == MAX_DATA_SPACE) return FILE_APPEND_FAILURE_MEMORY_FULL;
	else if(total_size > 0xFFFFFFFF) return FILE_APPEND_FAILURE_DATA_TOO_LARGE;
	else if(_file->open_mode == FILE_OPEN_MODE_READ) return FILE_APPEND_FAILURE_FILE_MODE_READONLY;

	if((_file->status != FILE_STATUS_DELETED) || (_file->status != FILE_STATUS_MAX_SIZE) || (_file->status != FILE_STATUS_READONLY))
	{
		if(_file->size != total_size) 
		{
			disk_readBlock(_file->info_block, __BUFFER);
			__create_8bit_number(total_size, &__BUFFER[FILE_SIZE_INDEX]);
			disk_writeBlock(_file->info_block, __BUFFER);

			clear_buffer();
		}
		uint32_t next_cluster = __getLastCluster(_file->start_cluster);
		uint16_t available_bytes = 0;
		uint16_t prev_data_length = 0;

		uint8_t read_buffer[(BLOCK_SIZE * CLUSTER)];
		uint16_t readIndex = 0;

		for(int i=0; i<CLUSTER; i++)
		{
			disk_readBlock((next_cluster + i), &read_buffer[readIndex]);

			readIndex += BLOCK_SIZE;
		}
		prev_data_length = strnlen(read_buffer, ((BLOCK_SIZE * CLUSTER) - 4));

		uint8_t* data_buffer = (uint8_t*)malloc(data_len + prev_data_length);

		strcpy(data_buffer, read_buffer);
		strcat(data_buffer, _data);

		uint32_t total_data_len = strlen(data_buffer);
		uint32_t appended_data_len = total_data_len - prev_data_length;
		
		float clusters_needed = (float)(total_data_len) / (float)(BLOCK_SIZE * CLUSTER);
		if(clusters_needed - (int)clusters_needed != 0) clusters_needed++;

		while((int)clusters_needed)
		{
			for(int i=0; i<CLUSTER; i++)
			{
				if(i != (CLUSTER - 1))
				{
					strncpy(__BUFFER, data_buffer + start_index, BLOCK_SIZE);

					disk_writeBlock((next_cluster + i), __BUFFER);

					start_index += BLOCK_SIZE;
					bytes_written += BLOCK_SIZE;

					if(bytes_written >= total_data_len) break;
				}
				else
				{
					strncpy(__BUFFER, data_buffer + start_index, BLOCK_SIZE - 4);

					disk_writeBlock((next_cluster + i), __BUFFER);

					start_index += BLOCK_SIZE - 4;
					bytes_written += BLOCK_SIZE - 4;

					if(bytes_written >= total_data_len) break;
				}
			}
			if((int)clusters_needed != 1)
			{
				CLUSTER_TYPE cluster_type;

				uint32_t cluster_to_link_to = next_cluster;
				next_cluster = __getFreeCluster(&cluster_type);

				disk_readBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
				__create_8bit_number(next_cluster, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
				disk_writeBlock((cluster_to_link_to + (CLUSTER - 1)), __BUFFER);
			}
			else
			{
				__create_8bit_number(0, &__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
				disk_writeBlock((next_cluster + (CLUSTER - 1)), __BUFFER);
			}
			clusters_needed--;
		}
		free(data_buffer);
	}
	return FILE_APPEND_SUCCESS;
}

void smfs_read(SMFS_FILE* _file, uint8_t* _buffer)
{
	uint32_t index = 0;
	uint32_t next_cluster = _file->start_cluster;

	while(next_cluster)
	{
		for(int i=0; i<CLUSTER; i++)
		{
			if(i != (CLUSTER - 1))
			{
				disk_readBlock((next_cluster + i), __BUFFER);

				strncpy((_buffer + index), __BUFFER, BLOCK_SIZE);

				if((int)strlen(__BUFFER) <= BLOCK_SIZE) break;

				index += BLOCK_SIZE;
			}
			else
			{
				disk_readBlock((next_cluster + i), __BUFFER);

				strncpy((_buffer + index), __BUFFER, BLOCK_SIZE - 4);

				index += BLOCK_SIZE - 4;

				if((int)strlen(__BUFFER) <= BLOCK_SIZE) break;
			}
		}
		next_cluster = __create_32bit_number(&__BUFFER[NEXT_CLUSTER_LINK_INDEX]);
	}
	uint32_t size = smfs_getFileSize(_file);

	float clusters_occupied = (float)size / (float)(BLOCK_SIZE * CLUSTER);
	if(clusters_occupied - (int)clusters_occupied != 0) clusters_occupied++;
	_file->size_on_disk = ((int)clusters_occupied * (CLUSTER * BLOCK_SIZE));
}

SMFS_FDELETE_RESULT smfs_delete(SMFS_FILE* _file)
{
	uint8_t file_info_buffer[BLOCK_SIZE];

	disk_readBlock(_file->info_block, file_info_buffer);
	
	uint32_t file_size = __create_32bit_number(&file_info_buffer[FILE_SIZE_INDEX]);
	float clusters_used = (float)file_size / (float)(BLOCK_SIZE * CLUSTER);
	if(clusters_used - (int)clusters_used != 0) clusters_used++;

	_file->status |= FILE_STATUS_DELETED;
	file_info_buffer[FILE_STATUS_INDEX] = _file->status;
	disk_writeBlock(_file->info_block, file_info_buffer);

	__readBlock0();
	uint32_t space_used = __create_32bit_number(&__BUFFER[TOTAL_SPACE_USED_INDEX]);
	space_used -= (int)clusters_used;
	__create_8bit_number(space_used, &__BUFFER[TOTAL_SPACE_USED_INDEX]);
	__TOTAL_FILES--;
	__create_8bit_number(__TOTAL_FILES, &__BUFFER[TOTAL_FILES_INDEX]);
	uint32_t deleted_files = __create_32bit_number(&__BUFFER[DELETED_FILES_INDEX]);
	deleted_files++;
	__create_8bit_number(deleted_files, &__BUFFER[DELETED_FILES_INDEX]);
	__writeBlock0();
	
	uint32_t first_deleted_file_info_block = __create_32bit_number(&__BUFFER[NEXT_DELETED_BLOCK_INDEX]);
	uint32_t last_deleted_file_info_block  = __create_32bit_number(&__BUFFER[LAST_DELETED_BLOCK_INDEX]);

	if(first_deleted_file_info_block == 0)
	{
		__create_8bit_number(_file->info_block, &__BUFFER[NEXT_DELETED_BLOCK_INDEX]);
		__create_8bit_number(_file->info_block, &__BUFFER[LAST_DELETED_BLOCK_INDEX]);
		__writeBlock0();
	}
	else
	{
		disk_readBlock(last_deleted_file_info_block, file_info_buffer);
		__create_8bit_number(_file->info_block, &file_info_buffer[DELETED_INFO_BLOCK_INDEX]);
		disk_writeBlock(last_deleted_file_info_block, file_info_buffer);
	}
	return FILE_DELETE_SUCCESS;
}