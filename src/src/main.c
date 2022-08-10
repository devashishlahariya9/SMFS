#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "smfs.h"

int main(void)
{
	system("cls");

	printf("\n\n\n\nSMFS SIMULATION STARTED...\n");

	SMFS_FILE file;

	smfs_format_disk();

	SMFS_STATUS fs_status = smfs_init();

	printf("\nFILE SYSTEM INITIALIZED...\n");

	if(fs_status == FS_OK)
	{
		printf("\nFILESYSTEM STATUS: FS_OK\n");

		uint64_t space_used = smfs_getTotalStorageUsed();
		float percentage_space_used = (((float)space_used / (float)DISK_MEMORY) * 100);
		printf("STORAGE USED: %d BYTES || %.2f%%\n", space_used, percentage_space_used);

		char file_data[] = "Hello, World!! SMFS Is Working...";

		smfs_create(&file, "Hello_World.txt");

		smfs_open(&file, "Hello_World.txt", FILE_OPEN_MODE_WRITE);

		SMFS_FWRITE_RESULT fwrite_res1 = smfs_write(&file, file_data);

		uint8_t file_buffer[50];

		smfs_read(&file, file_buffer);

		uint64_t space_used = smfs_getTotalStorageUsed();
		float percentage_space_used = (((float)space_used / (float)DISK_MEMORY) * 100);
		printf("STORAGE USED: %d BYTES || %.2f%%\n", space_used, percentage_space_used);
	}
	else
	{
		printf("\033[1;31m");
		printf("FILESYSTEM STATUS: FS_NO_FILESYSTEM\n");
	}
	return 0;
}