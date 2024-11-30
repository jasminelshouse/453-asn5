
#define PARTITION_TABLE_LOC 0x1BE /* location of the partition table */
#define PARTITION_TYPE 0x81 /* partition type for Minix */
#define BYTE_510 0x55 /*byte 510 of a boot sector with a valid partition table*/
#define BYTE_511 0xAA /* byte 511 of a boot sector with a valid partition table */
#define MAGIC_NUM 0x4D5A /* the minix magic number */
#define R_MAGIC_NUM 0x5A4D /* minix magic number on a byte-reversed filesystem */
#define INODE_SIZE 64 /* size of an inode in bytes */ 
#define DIRECTORY_ENTRY_SIZE 64 /* size of a directory entry in bytes */ 

#define DIRECT ZONES 7

#define FILE_TYPE 0170000 /* File type mask */ 
#define REGULAR_FILE 0100000 /* Regular file */ 
#define DIRECTORY 0040000 /* Directory */ 
#define OR_PERMISSION 0000400 /* Owner read permission */
#define OW_PERMISSION 0000200 /* Owner write permission */ 
#define OE_PERMISSION 0000100 /* Owner execute permission */ 
#define GR_PERMISSION 0000040 /* Group read permission */ 
#define GW_PERMISSION 0000020 /* Group write permission */ 
#define GE_PERMISSION 0000010 /* Group execute permission */ 
#define OR_PERMISSION 0000004 /* Other read permission */ 
#define OW_PERMISSION 0000002 /* Other write permission */ 
#define OE_PERMISSION 0000001 /* Other execute permission */


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
};


struct superblock { /* Minix Version 3 Superblock
    * this structure found in fs/super.h
    * in minix 3.1.1
    */
    /* on disk. These fields and orientation are non–negotiable */
    uint32 t ninodes; /* number of inodes in this filesystem */
    uint16 t pad1; /* make things line up properly */
    int16 t i blocks; /* # of blocks used by inode bit map */
    int16 t z blocks; /* # of blocks used by zone bit map */
    uint16 t firstdata; /* number of first data zone */
    int16 t log zone size; /* log2 of blocks per zone */
    int16 t pad2; /* make things line up again */
    uint32 t max file; /* maximum file size */
    uint32 t zones; /* number of zones on disk */
    int16 t magic; /* magic number */
    int16 t pad3; /* make things line up again */
    uint16 t blocksize; /* block size in bytes */
    uint8 t subversion; /* filesystem sub–version */
}


struct inode {
    uint16 t mode; /* mode */
    uint16 t links; /* number or links */
    uint16 t uid;
    uint16 t gid;
    uint32 t size;
    int32 t atime;
    int32 t mtime;
    int32 t ctime;
    uint32 t zone[DIRECT ZONES];
    uint32 t indirect;
    uint32 t two indirect;
    uint32 t unused;
};