#ifndef SMFSCONFIG_H
#define SMFSCONFIG_H

#include <stdint.h>

#define USE_MEMORY_FORMAT_KB            //Uncomment And Use Accordingly
// #define USE_MEMORY_FORMAT_MB
// #define USE_MEMORY_FORMAT_GB

#define MEMORY							200
#define BLOCK_SIZE 						512         //Do Not Change
#define MAX_NUMBER_OF_FILES				100000      //Set The Max Number Of Files Here

#ifdef USE_MEMORY_FORMAT_KB
	#define DISK_MEMORY					(MEMORY * 1024)
	#define MAX_BLOCKS					(SD_MEMORY / BLOCK_SIZE)
#elif USE_MEMORY_FORMAT_MB
	#define DISK_MEMORY					((MEMORY * 1024) * 1024)
	#define MAX_BLOCKS					(SD_MEMORY / BLOCK_SIZE)
#else
	#define DISK_MEMORY					8
	#define MAX_BLOCKS					((((MEMORY * 1024) * 1024) * 1024) / BLOCK_SIZE)
#endif

#endif