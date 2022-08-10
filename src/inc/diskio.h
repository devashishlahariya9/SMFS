#ifndef DISKIO_H
#define DISKIO_H

void disk_init(void);
void disk_readBlock(uint32_t _blockNumber, uint8_t* _buffer);
void disk_writeBlock(uint32_t _blockNumber, uint8_t* _buffer);

#endif