#ifndef MINGET_H
#define MINGET_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SECTOR_SIZE 512
#define BOOT_SIG_OFFSET 510
#define PARTITION_TABLE_OFFSET 446
#define EXTENDED_PARTITION 0x05

#define PARTITION_TABLE_LOC 0x1BE /* location of the partition table */
#define PARTITION_TYPE 0x81 /* partition type for Minix */
#define BYTE_510 0x55 /*byte 510 of a boot sector with a valid partition table*/
#define BYTE_511 0xAA /* byte 511 of a boot sector with valid partition table */
#define MAGIC_NUM 0x4D5A /* the minix magic number */
#define R_MAGIC_NUM 0x5A4D /* minix magic number on byte-reversed filesystem */
#define INODE_SIZE 64 /* size of an inode in bytes */ 
#define DIRECTORY_ENTRY_SIZE 64 /* size of a directory entry in bytes */ 

#define DIRECT_ZONES 7

#define FILE_TYPE 0170000 /* File type mask */ 
#define REGULAR_FILE 0100000 /* Regular file */ 
#define DIRECTORY 0040000 /* Directory */ 
#define OWR_PERMISSION 0000400 /* Owner read permission */
#define OWW_PERMISSION 0000200 /* Owner write permission */ 
#define OWE_PERMISSION 0000100 /* Owner execute permission */ 
#define GR_PERMISSION 0000040 /* Group read permission */ 
#define GW_PERMISSION 0000020 /* Group write permission */ 
#define GE_PERMISSION 0000010 /* Group execute permission */ 
#define OTR_PERMISSION 0000004 /* Other read permission */ 
#define OTW_PERMISSION 0000002 /* Other write permission */ 
#define OTE_PERMISSION 0000001 /* Other execute permission */

#ifndef DIRSIZ
#define DIRSIZ 60
#endif



struct partition_table {
	uint8_t bootind;        /* Boot magic number (0x80 if bootable) */
	uint8_t start_head;     /* Start of partition in CHS   */
	uint8_t start_sec;
	uint8_t start_cyl;
	uint8_t type;           /* Type of partition (0x81 is minix) */
	uint8_t end_head;       /* End of partition in CHS   */
	uint8_t end_sec;
	uint8_t end_cyl;
	uint32_t IFirst;        /* First sector (LBA addressing) */
	uint32_t size;          /* size of partition (in sectors) */
} __attribute__((packed));


struct superblock { /* Minix Version 3 Superblock
    * this structure found in fs/super.h
    * in minix 3.1.1
    */
    /* on disk. These fields and orientation are non–negotiable */
    uint32_t ninodes; /* number of inodes in this filesystem */
    uint16_t pad1; /* make things line up properly */
    int16_t i_blocks; /* # of blocks used by inode bit map */
    int16_t z_blocks; /* # of blocks used by zone bit map */
    uint16_t firstdata; /* number of first data zone */
    int16_t log_zone_size; /* log2 of blocks per zone */
    int16_t pad2; /* make things line up again */
    uint32_t max_file; /* maximum file size */
    uint32_t zones; /* number of zones on disk */
    int16_t magic; /* magic number */
    int16_t pad3; /* make things line up again */
    uint16_t blocksize; /* block size in bytes */
    uint8_t subversion; /* filesystem sub–version */
} __attribute__((packed));


struct inode {
    uint16_t mode; /* mode */
    uint16_t links; /* number or links */
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    int32_t atime;
    int32_t mtime;
    int32_t c_time;
    uint32_t zone[DIRECT_ZONES];
    uint32_t indirect;
    uint32_t two_indirect;
    uint32_t unused;
} __attribute__((packed));



struct fileent {
    uint32_t ino;
    char name[DIRSIZ];
} __attribute__((packed));




#endif /*MINGET_H*/