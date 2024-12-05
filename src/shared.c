#include "shared.h"

/* Shared implementations */

void read_superblock(FILE *file, struct superblock *sb, 
        int partition_offset, int verbose) {
    
    /* move fp to location of superblock*/
    fseek(file, partition_offset + 1024, SEEK_SET);

    /* read sb data into structure */
    fread(sb, sizeof(struct superblock), 1, file);

    /* validate magic num */
    if (sb->magic != MAGIC_NUM && sb->magic != R_MAGIC_NUM) {

        /* error message if not valid */
        fprintf(stderr, "Bad magic number. (0x%x)\nThis doesn’t look like "
               "a MINIX filesystem.\n", sb->magic);
        exit(EXIT_FAILURE);
    }
}


void read_inode(FILE *file, int inode_num, struct inode *inode,
                struct superblock *sb) {
    int inodes_per_block = sb->blocksize / INODE_SIZE;
    int inode_start_block = 2 + sb->i_blocks + sb->z_blocks;
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block;
    int inode_index = (inode_num - 1) % inodes_per_block;
    long inode_offset = (inode_block * sb->blocksize) + 
        (inode_index * INODE_SIZE);

    /* seek calculated inode offset within file */
    if (fseek(file, inode_offset, SEEK_SET) != 0) {
        perror("Failed to seek to inode position");
        return;
    }

    /* buffer to hold the raw inode data read from file */
    unsigned char raw_inode[INODE_SIZE];
    if (fread(raw_inode, INODE_SIZE, 1, file) != 1) {
        perror("Failed to read inode from disk");
        return;
    }

    /* copy into inode structure */
    memcpy(inode, raw_inode, sizeof(struct inode));
}

void print_inode(struct inode *inode) {
    printf("\nFile inode:\n");
    printf("  unsigned short mode       0x%x    (%s)\n", inode->mode, 
        get_permissions(inode->mode));
    printf("  unsigned short links         %d\n", inode->links);
    printf("  unsigned short uid           %d\n", inode->uid);
    printf("  unsigned short gid           %d\n", inode->gid);
    printf("  uint32_t size                %u\n", inode->size);

    /* safely handle packed ones*/
    time_t access_time = inode->atime;
    time_t mod_time = inode->mtime;
    time_t change_time = inode->c_time;  

    printf("  uint32_t atime     %u    --- %s", inode->atime, 
        ctime(&access_time));
    printf("  uint32_t mtime     %u    --- %s", inode->mtime, 
        ctime(&mod_time));
    printf("  uint32_t ctime     %u    --- %s", inode->c_time, 
        ctime(&change_time));

    printf("\nDirect zones:\n");
    int i;
    for (i = 0; i < DIRECT_ZONES; i++) {
        printf("  zone[%d]   = %u\n", i, inode->zone[i]);
    }
    printf("  uint32_t indirect   = %u\n", inode->indirect);
    printf("  uint32_t double     = %u\n", inode->two_indirect);
}

int traverse_directory(FILE *file, struct inode *current_inode,
                       const char *entry_name, 
                        struct inode *found_inode, struct superblock *sb) {
    int i;
    char *buffer;
    buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return 0;
    }
    
    for (i = 0; i < DIRECT_ZONES; i++) {
        if (current_inode->zone[i] == 0) {
            /* printf("DEBUG: Zone %d is empty, skipping.\n", i);*/
            continue;
        }

        /* Calculate block address*/
        int block_address = sb->firstdata + (current_inode->zone[i] - 1);
        if (block_address < sb->firstdata) {
            fprintf(stderr, "Error: Invalid block address %d in zone %d\n",
                 block_address, i);
            continue;
        }

        /* Read directory block*/
        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, sb->blocksize, 1, file);

        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);

            if (entry->ino == 0) {  
                offset += sizeof(struct fileent);
                continue;
            }

            /* printf("DEBUG: Directory entry: name='%s', ino=%d\n", 
            entry->name, entry->ino);*/

            if (strcmp(entry->name, entry_name) == 0) {
                /* Found the target entry*/
                read_inode(file, entry->ino, found_inode, sb);
                free(buffer);
                return 1;  
            }

            offset += sizeof(struct fileent);
        }
    }

    free(buffer);
    return 0;  
}


int find_inode_by_path(FILE *file, const char *path, 
    struct inode *inode, struct superblock *sb) {
    if (strcmp(path, "/") == 0) {
        read_inode(file, 1, inode, sb);  
        return 0;
    }

    char *path_copy = malloc(strlen(path) + 1);
    char *token = strtok(path_copy, "/");
    struct inode current_inode;
    read_inode(file, 1, &current_inode, sb);  
    /** tokenize and traverse each component in the path **/
    while (token != NULL) {
        if (!(current_inode.mode & DIRECTORY)) {
            fprintf(stderr, "Error: '%s' is not a directory.\n", token);
            free(path_copy);
            return -1;
        }

        if (!traverse_directory(file, &current_inode, token,
             &current_inode, sb)) {
            fprintf(stderr, "Error: Path component '%s' not found.\n",
                 token);
            free(path_copy);
            return -1;
        }

        token = strtok(NULL, "/");
    }
    
    *inode = current_inode;  
    free(path_copy);
    return 0;
}

void print_usage_minget() {
printf("Usage: minget [-v] [-p part [-s sub]] imagefile srcpath [dstpath]\n");
printf("Options:\n"
    "\t-p\t part    --- select partition for filesystem (default: none)\n"
    "\t-s\t sub     --- select subpartition for filesystem (default: none)\n"
    "\t-h\t help    --- print usage information and exit\n"
    "\t-v\t verbose --- increase verbosity level\n");
}

void print_usage_minls() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
    printf("Options:\n"
    "\t-p\t part    --- select partition for filesystem (default: none)\n"
    "\t-s\t sub     --- select subpartition for filesystem (default: none)\n"
    "\t-h\t help    --- print usage information and exit\n"
    "\t-v\t verbose --- increase verbosity level\n");
}

void read_partition_table(FILE *file, int partition, int subpartition,
     int *partition_offset) {
    uint8_t buffer[SECTOR_SIZE];
    /** read first sector to access partition table. **/
    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    struct partition_table *partitions = 
        (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

    
    if (partition < 0 || partition >= 4) {
        fprintf(stderr, "Invalid primary partition number: %d\n", partition);
        exit(EXIT_FAILURE);
    }

    /** compute offset for selected primary partition. **/
    uint32_t actual_sector = partitions[partition].IFirst;
    *partition_offset = actual_sector * SECTOR_SIZE;
    /*was getting negative numbers for a while so 
    implemented this check*/
    if (*partition_offset < 0) {
    fprintf(stderr, "Error: Partition offset is invalid.\n");
    exit(EXIT_FAILURE);
    }

    /*printf("Primary Partition %d: IFirst=%u, size=%u, 
        Offset=%d bytes\n",partition, actual_sector, 
        partitions[partition].size, *partition_offset);*/

    if (subpartition != -1) {
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, SECTOR_SIZE, 1, file);

        struct partition_table *subpartitions = 
            (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

        if (subpartition < 0 || subpartition >= 4) {
            fprintf(stderr, "Invalid subpartition number: %d\n", 
                subpartition);
            exit(EXIT_FAILURE);
        }
        /** compute subpartition offset. **/
        uint32_t sub_actual_sector = subpartitions[subpartition].IFirst;
        int subpartition_offset = sub_actual_sector * SECTOR_SIZE;

        /*printf("DEBUG: Primary partition offset: %d bytes\n",
             *partition_offset);
        printf("DEBUG: Subpartition offset: %d bytes\n", 
            subpartition_offset);*/
     /** update final partition offset to include subpartition. **/
        *partition_offset = subpartition_offset;

       
    }
}
