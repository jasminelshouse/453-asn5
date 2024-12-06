#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minget.h"

/* function declarations */
void read_partition_table(FILE *file, int partition, int subpartition, 
    int *partition_offset);
void read_superblock(FILE *file, struct superblock *sb, int partition_offset, 
    int verbose);
int find_inode_by_path(FILE *file, const char *path, struct inode *inode, 
    struct superblock *sb);
void print_inode(struct inode *inode);
void copy_file_contents(FILE *file, struct inode *inode, struct superblock *sb, 
    int partition_offset, FILE *output);


void print_usage() {
printf("Usage: minget [-v] [-p part [-s sub]] imagefile srcpath [dstpath]\n");
printf("Options:\n"
    "\t-p\t part    --- select partition for filesystem (default: none)\n"
    "\t-s\t sub     --- select subpartition for filesystem (default: none)\n"
    "\t-h\t help    --- print usage information and exit\n"
    "\t-v\t verbose --- increase verbosity level\n");
}


void read_superblock(FILE *file, struct superblock *sb, 
        int partition_offset, int verbose) {
    /* for typical offset, and 3*512 alignment*/
    int possible_offsets[] = {0, 1536, 1024, 2048}; 
    int num_offsets = 
        sizeof(possible_offsets) / sizeof(possible_offsets[0]);
    long superblock_offset;
    int valid_superblock_found = 0;

    if (verbose) {
        /* printf("DEBUG: Starting superblock read. 
        Partition offset: %d\n", partition_offset);*/
    }
    int i;
    for (i=0; i < num_offsets; i++) {
        superblock_offset = partition_offset + possible_offsets[i];

        if (verbose) {
            /* printf("DEBUG: Trying superblock offset: %ld\n", 
        superblock_offset);*/
        }

        if (fseek(file, superblock_offset, SEEK_SET) != 0) {
            perror("Failed to seek to superblock");
            continue;
        }

        if (fread(sb, sizeof(struct superblock), 1, file) != 1) {
            perror("Failed to read superblock");
            continue;
        }

        if (sb->magic == MAGIC_NUM || sb->magic == MAGIC_NUM_OLD ||
            sb->magic == R_MAGIC_NUM || sb->magic == R_MAGIC_NUM_OLD) {
            valid_superblock_found = 1;
            if (verbose) {
                /* printf("DEBUG: Valid superblock found at offset: %ld\n", 
                superblock_offset);*/
            }
            break;
        } else {
            if (verbose) {
                /*printf("DEBUG: Invalid magic number: 0x%x at offset: 
                %ld\n", sb->magic, superblock_offset);*/
            }
        }
    }

    if (!valid_superblock_found) {
        fprintf(stderr, "ERROR: Failed to locate a valid superblock.\n");
        exit(EXIT_FAILURE);
    }
}


void read_inode(FILE *file, int inode_offset, struct inode *inode, 
    const struct superblock *sb) {
    char buffer[INODE_SIZE];

    /* read raw inode data*/
    fseek(file, inode_offset, SEEK_SET);
    fread(buffer, INODE_SIZE, 1, file);

    /* map raw data to struct inode*/
    inode->mode = *(uint16_t *)&buffer[0];
    inode->links = *(uint16_t *)&buffer[2];
    inode->uid = *(uint16_t *)&buffer[4];
    inode->gid = *(uint16_t *)&buffer[6];
    inode->size = *(uint32_t *)&buffer[8];
    inode->atime = *(uint32_t *)&buffer[12];
    inode->mtime = *(uint32_t *)&buffer[16];
    inode->c_time = *(uint32_t *)&buffer[20];
    int i;
    /** populate direct zone ptrs from raw inode buffer **/
    for ( i = 0; i < DIRECT_ZONES; i++) {
        inode->zone[i] = *(uint32_t *)&buffer[24 + (i * 4)];
    }
    inode->indirect = *(uint32_t *)&buffer[52];
    inode->two_indirect = *(uint32_t *)&buffer[56];
}

/* self explanatory */
const char *get_permissions(uint16_t mode) {
    static char perms[11];
    perms[0] = (mode & DIRECTORY) ? 'd' : '-';
    perms[1] = (mode & OWR_PERMISSION) ? 'r' : '-';
    perms[2] = (mode & OWW_PERMISSION) ? 'w' : '-';
    perms[3] = (mode & OWE_PERMISSION) ? 'x' : '-';
    perms[4] = (mode & GR_PERMISSION) ? 'r' : '-';
    perms[5] = (mode & GW_PERMISSION) ? 'w' : '-';
    perms[6] = (mode & GE_PERMISSION) ? 'x' : '-';
    perms[7] = (mode & OTR_PERMISSION) ? 'r' : '-';
    perms[8] = (mode & OTW_PERMISSION) ? 'w' : '-';
    perms[9] = (mode & OTE_PERMISSION) ? 'x' : '-';
    perms[10] = '\0';
    return perms;
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
    char *buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return 0;
    }
    int i;
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

    char *path_copy = strdup(path);
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


void copy_file_contents(FILE *file, struct inode *inode, struct superblock *sb,
 int partition_offset, FILE *output) {
    /* simple implementation assuming all data is in direct zones */
    char buffer[sb->blocksize];
    int i;
    for (i = 0; i < DIRECT_ZONES; i++) {
        if (inode->zone[i] == 0) continue;  /* skip empty zones */

        int block_address = partition_offset + inode->zone[i] * sb->blocksize;
        fseek(file, block_address, SEEK_SET);
        int to_read = 
        (inode->size < sb->blocksize) ? inode->size : sb->blocksize;
        fread(buffer, to_read, 1, file);
        fwrite(buffer, to_read, 1, output);

        inode->size -= to_read;
        if (inode->size <= 0) break;  /* stop if no more data */
    }
}


int main(int argc, char *argv[]) {
    int verbose = 0;
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *srcpath = NULL;
    char *dstpath = NULL;
    FILE *file;
    FILE *output = stdout;
    struct superblock sb;
    struct inode src_inode;

    /* argument parsing logic */
    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-v") == 0) {
                verbose = 1;
            } else if (strcmp(argv[i], "-p") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "Error: Missing value for -p\n");
                    print_usage();
                    return EXIT_FAILURE;
                }
                partition = atoi(argv[i]);
            } else if (strcmp(argv[i], "-s") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "Error: Missing value for -s\n");
                    print_usage();
                    return EXIT_FAILURE;
                }
                subpartition = atoi(argv[i]);
            } else {
                fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                print_usage();
                return EXIT_FAILURE;
            }
        } else if (!imagefile) {
            imagefile = argv[i];
        } else if (!srcpath) {
            srcpath = argv[i];
        } else if (!dstpath) {
            dstpath = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (!imagefile || !srcpath) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage();
        return EXIT_FAILURE;
    }

    /* open image file */
    file = fopen(imagefile, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open image file '%s'\n", imagefile);
        return EXIT_FAILURE;
    }

    /* if destination path is provided, open destination file for writing */
    if (dstpath) {
        output = fopen(dstpath, "wb");
        if (!output) {
            fprintf(stderr, "Error: Cannot open destination file '%s'\n", 
            dstpath);
            fclose(file);
            return EXIT_FAILURE;
        }
    }

    /* read partition and subpart info */
    int partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
    }

    /* read superblock of selected partition */
    read_superblock(file, &sb, partition_offset, verbose);

    /* find target inode based on the provided path */
    if (find_inode_by_path(file, srcpath, &src_inode, &sb) != 0) {
        fprintf(stderr, "Error: Path not found '%s'\n", srcpath);
        fclose(file);
        if (output != stdout) fclose(output);
        return EXIT_FAILURE;
    }

    /* output inode information if verbose */
    if (verbose) {
        print_inode(&src_inode);
    }

    /* copy file contents from source inode to output */
    copy_file_contents(file, &src_inode, &sb, partition_offset, output);

    /* clean up */
    fclose(file);
    if (output != stdout) fclose(output);

    return EXIT_SUCCESS;
}
