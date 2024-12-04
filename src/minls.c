#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minls.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
}

void read_superblock(FILE *file, struct superblock *sb, int partition_offset) {
    long superblock_offset = partition_offset + 1024;
    
    printf("partition_offset read=%d\n", partition_offset);
    printf("superblock_offsetread=%ld\n", superblock_offset);
    // First check at partition_offset
    fseek(file, superblock_offset, SEEK_SET);
    fread(sb, sizeof(struct superblock), 1, file);

    // if (sb->magic != MAGIC_NUM && sb->magic != R_MAGIC_NUM) {
    //     // If no valid magic number, check partition_offset + 1024
    //     superblock_offset = partition_offset + 1024;
    //     fseek(file, superblock_offset, SEEK_SET);
    //     fread(sb, sizeof(struct superblock), 1, file);
    // }

    // // Validate the superblock magic number
    // if (sb->magic != MAGIC_NUM && sb->magic != R_MAGIC_NUM) {
    //     fprintf(stderr, "Bad magic number. (0x%x) at offset %ld\n",
    //             sb->magic, superblock_offset);
    //     fprintf(stderr, "This doesn’t look like a MINIX filesystem.\n");
    //     exit(EXIT_FAILURE);
    // }

    printf("Superblock Magic: 0x%x at Offset: %ld\n", sb->magic, superblock_offset);
}

void print_computed_fields(struct superblock *sb) {
    int zone_size = sb->blocksize * (1 << sb->log_zone_size);
    int ptrs_per_zone = zone_size / sizeof(uint32_t); // Number of pointers in a zone
    int ino_per_block = sb->blocksize / INODE_SIZE;   // Number of inodes per block
    int fileent_size = DIRECTORY_ENTRY_SIZE;          // Size of a directory entry (assumed)
    int ent_per_zone = zone_size / fileent_size;      // Number of directory entries per zone

    // Compute version based on magic number
    int version = (sb->magic == MAGIC_NUM) ? 3 : (sb->magic == MAGIC_NUM_OLD) ? 2 : 0;

    // First inode map, zone map, and inode block
    int firstImap = 2; // Typically starts at block 2
    int firstZmap = firstImap + sb->i_blocks;
    int firstIblock = firstZmap + sb->z_blocks;

    // wrongended (check for byte-swapped magic number)
    int wrongended = (sb->magic == R_MAGIC_NUM || sb->magic == R_MAGIC_NUM_OLD) ? 1 : 0;

    // Maximum filename length (assumed constant)
    int max_filename = DIRSIZ;

    printf("\nComputed Fields:\n");
    printf("  version            %d\n", version);
    printf("  firstImap          %d\n", firstImap);
    printf("  firstZmap          %d\n", firstZmap);
    printf("  firstIblock        %d\n", firstIblock);
    printf("  zonesize        %d\n", zone_size);
    printf("  ptrs_per_zone   %d\n", ptrs_per_zone);
    printf("  ino_per_block     %d\n", ino_per_block);
    printf("  wrongended         %d\n", wrongended);
    printf("  fileent_size      %d\n", fileent_size);
    printf("  max_filename      %d\n", max_filename);
    printf("  ent_per_zone      %d\n", ent_per_zone);
}



/* self explanatory */
void print_superblock(struct superblock *sb) {
    int zone_size = sb->blocksize * (1 << sb->log_zone_size);
    printf("\nSuperblock Contents:\nStored Fields:\n");
    printf("  ninodes %u\n", sb->ninodes);
    printf("  i_blocks %d\n", sb->i_blocks);
    printf("  z_blocks %d\n", sb->z_blocks);
    printf("  firstdata %u\n", sb->firstdata);
    printf("  log_zone_size %d (zone size: %d)\n", sb->log_zone_size,
        zone_size);
    printf("  max_file %u\n", sb->max_file);
    printf("  magic 0x%x\n", sb->magic);
    printf("  zones %u\n", sb->zones);
    printf("  blocksize %u\n", sb->blocksize);
    printf("  subversion %u\n", sb->subversion);
}

void read_inode(FILE *file, int inode_num, struct inode *inode,
                struct superblock *sb) {
    printf("DEBUG: Starting read_inode for inode_num=%d\n", inode_num);

    // Calculate inodes per block
    int inodes_per_block = sb->blocksize / INODE_SIZE;
    printf("DEBUG: inodes_per_block=%d\n", inodes_per_block);

    // Calculate starting block for inodes
    int inode_start_block = 2 +sb->i_blocks + sb->z_blocks;
    printf("DEBUG: inode_start_block=%d\n", inode_start_block);

    // Calculate block and index for the inode
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block;
    int inode_index = (inode_num - 1) % inodes_per_block;
    printf("DEBUG: inode_block=%d, inode_index=%d\n", inode_block, inode_index);

    // Calculate byte offset for the inode
    long inode_offset = (inode_block * sb->blocksize) + 
                        (inode_index * INODE_SIZE);
    printf("DEBUG: Calculated inode_offset=%ld\n", inode_offset);

    // Seek to the inode position in the file
    if (fseek(file, inode_offset, SEEK_SET) != 0) {
        perror("Failed to seek to inode position");
        printf("DEBUG: Failed to seek to inode_offset=%ld\n", inode_offset);
        return;
    }
    printf("DEBUG: Seek to inode position successful\n");

    // Buffer to hold raw inode data
    unsigned char raw_inode[INODE_SIZE];
    if (fread(raw_inode, INODE_SIZE, 1, file) != 1) {
        perror("Failed to read inode from disk");
        printf("DEBUG: Failed to read %d bytes from offset=%ld\n", INODE_SIZE, inode_offset);
        return;
    }
    printf("DEBUG: Read raw inode data successfully\n");

    // Print raw inode data for debugging
    printf("DEBUG: Raw inode data: ");
    for (int i = 0; i < INODE_SIZE; i++) {
        printf("%02x ", raw_inode[i]);
    }
    printf("\n");

    // Copy raw data into inode structure
    memcpy(inode, raw_inode, sizeof(struct inode));
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

void list_directory(FILE *file, struct inode *dir_inode, struct superblock *sb) {
    printf("DEBUG: Entering list_directory\n");
    if (!(dir_inode->mode & DIRECTORY)) {
        fprintf(stderr, "Error: Not a directory.\n");
        return;
    }

    printf("DEBUG: Allocating buffer of size %d\n", sb->blocksize);
    char *buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed.\n");
        return;
    }

    printf("/:\n");
    printf("DEBUG: Iterating through direct zones\n");

    /* iterate through direct zones of directory inode */
    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) {
            printf("DEBUG: zone[%d] is empty, skipping\n", i);
            continue;
        }

        // Correct calculation of the block address
        long block_address = (long)dir_inode->zone[i] * sb->blocksize;
        printf("DEBUG: Reading block at zone[%d] = %d (block address: %ld)\n", i, dir_inode->zone[i], block_address);

        /* seek to calculated block position in file */
        if (fseek(file, block_address, SEEK_SET) != 0) {
            perror("Failed to seek to block position");
            continue;
        }

        if (fread(buffer, sb->blocksize, 1, file) != 1) {
            perror("Failed to read block data");
            continue;
        }

        printf("DEBUG: Processing entries in block\n");
        /* process each directory entry within block */
        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);
            printf("DEBUG: Entry at offset %d: ino=%d, name=%s\n", offset, entry->ino, entry->name);

            if (entry->ino != 0) {
                printf("DEBUG: Valid entry found, inode=%d, name=%s\n", entry->ino, entry->name);
                struct inode entry_inode;
                printf("DEBUG: Calling read_inode for inode %d\n", entry->ino);
                read_inode(file, entry->ino, &entry_inode, sb);

                printf("DEBUG: Entry details - permissions=%s, size=%d, name=%s\n",
                       get_permissions(entry_inode.mode),
                       entry_inode.size,
                       entry->name);

                printf("%s %5d %s\n",
                       get_permissions(entry_inode.mode),
                       entry_inode.size,
                       entry->name);
            }

            /* move to next entry */
            offset += sizeof(struct fileent);
        }
    }

    /* free buffer */
    free(buffer);
    printf("DEBUG: Exiting list_directory\n");
}


int traverse_directory(FILE *file, struct inode *current_inode, 
    const char *entry_name, struct inode *found_inode, struct superblock *sb) {
    char buffer[4096]; 
    struct fileent *entry;
    int i, offset;

    for (i = 0; i < DIRECT_ZONES; i++) {
        if (current_inode->zone[i] == 0)
            continue;

        int block_address = sb->firstdata + (current_inode->zone[i] - 1);
        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, 1, sb->blocksize, file);
        offset = 0;

        while (offset < sb->blocksize) {
            entry = (struct fileent *)(buffer + offset);
            if (entry->ino != 0 && strcmp(entry->name, entry_name) == 0) {
                read_inode(file, entry->ino, found_inode, sb);
                return 1; /* found */
            }
            offset += sizeof(struct fileent);
        }
    }
    return 0; /* not found */
}


void print_inode(struct inode *inode) {
    int i; 
    printf("\nFile inode:\n");
    printf("  uint16_t mode 0x%x (%s)\n", inode->mode, 
    get_permissions(inode->mode));
    printf("  uint16_t links %d\n", inode->links);
    printf("  uint16_t uid %d\n", inode->uid);
    printf("  uint16_t gid %d\n", inode->gid);
    printf("  uint32_t size %u\n", inode->size);
    time_t atime = inode->atime;
    printf("  uint32_t atime %u --- %s", inode->atime, ctime(&atime));
    time_t mtime = inode->mtime;
    printf("  uint32_t mtime %u --- %s", inode->mtime, ctime(&mtime));
    time_t c_time = inode->c_time;
    printf("  uint32_t ctime %u --- %s", inode->c_time, ctime(&c_time));
    printf("\nDirect zones:\n");
    for (i = 0; i < DIRECT_ZONES; i++) {
        printf("  zone[%d] = %u\n", i, inode->zone[i]);
    }
    printf("uint32_t indirect %u\n", inode->indirect);
    printf("uint32_t double %u\n", inode->two_indirect);
    printf("\n");
}

int find_inode_by_path(FILE *file, const char *path, struct inode *inode,
 struct superblock *sb) {
    if (strcmp(path, "/") == 0) {
        read_inode(file, 1, inode, sb); /* root inode */
        return 0;
    }

    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    struct inode current_inode;
    read_inode(file, 1, &current_inode, sb);  /* start at root*/
    while (token != NULL) {
        if (!S_ISDIR(current_inode.mode)) { /* check if curr inode is direct*/
            fprintf(stderr, "Not a directory\n");
            free(path_copy);
            return -1;
        }
        printf("Resolving token: %s\n", token);
        if (!traverse_directory(file, &current_inode, token, 
            &current_inode, sb)) {  /* find token in curr directory */
            fprintf(stderr, "Path not found: %s\n", token);
            free(path_copy);
            return -1;
        }
        token = strtok(NULL, "/");
    }

    *inode = current_inode; /* found inode of last token */
    free(path_copy);
    return 0;
}

void read_partition_table(FILE *file, int partition, int subpartition, int *partition_offset) {
    uint8_t buffer[SECTOR_SIZE];
    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    if (buffer[BOOT_SIG_OFFSET] != 0x55 || buffer[BOOT_SIG_OFFSET + 1] != 0xAA) {
        fprintf(stderr, "Invalid partition table signature (Expected: 0x55AA, Found: 0x%02X%02X)\n",
                buffer[BOOT_SIG_OFFSET], buffer[BOOT_SIG_OFFSET + 1]);
        exit(EXIT_FAILURE);
    }

    struct partition_table *partitions = (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

    if (partition < 0 || partition >= 4) {
        fprintf(stderr, "Invalid primary partition number: %d\n", partition);
        exit(EXIT_FAILURE);
    }

    uint32_t actual_sector = partitions[partition].IFirst; // Assume already in LBA
    printf("DEBUG: Actual sector: %u\n", actual_sector);

    // Calculate primary partition offset
    *partition_offset = actual_sector * SECTOR_SIZE;
    printf("Primary Partition %d: IFirst=%u, size=%u, Offset=%d bytes\n",
           partition, actual_sector, partitions[partition].size, *partition_offset);

    if (subpartition != -1) {
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, SECTOR_SIZE, 1, file);

        struct partition_table *subpartitions = (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

        if (subpartition < 0 || subpartition >= 4) {
            fprintf(stderr, "Invalid subpartition number: %d\n", subpartition);
            exit(EXIT_FAILURE);
        }

        uint32_t sub_actual_sector = subpartitions[subpartition].IFirst; // Assume already in LBA

        // Add subpartition offset to the primary partition offset
        *partition_offset += sub_actual_sector * SECTOR_SIZE;
        printf("Subpartition %d: IFirst=%u, size=%u, Final Offset=%d bytes\n",
               subpartition, sub_actual_sector, subpartitions[subpartition].size, *partition_offset);
    }
}

void print_partition_table(FILE *file, int partition_offset, int print_subpartitions) {
    fseek(file, partition_offset, SEEK_SET);
    uint8_t buffer[SECTOR_SIZE];
    fread(buffer, SECTOR_SIZE, 1, file);

    struct partition_table *partitions = (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);
    printf("Partition table:\n");
    printf("       ----Start----      ------End-----\n");
    printf("  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");

    for (int i = 0; i < 4; i++) {
        // Correct CHS interpretation
        int start_cyl = (partitions[i].start_sec & 0xC0) << 2 | partitions[i].start_cyl;
        int end_cyl = (partitions[i].end_sec & 0xC0) << 2 | partitions[i].end_cyl;
        printf("  0x%02X    %2d    %2d  %4d 0x%02X    %2d   %2d  %4d %10u %10u\n",
               partitions[i].bootind,
               partitions[i].start_head,
               partitions[i].start_sec & 0x3F,  // Masking the lower 6 bits
               start_cyl,
               partitions[i].type,
               partitions[i].end_head,
               partitions[i].end_sec & 0x3F,  // Masking the lower 6 bits
               end_cyl,
               partitions[i].IFirst,
               partitions[i].size);
    }

    if (print_subpartitions) {
        for (int i = 0; i < 4; i++) {
            if (partitions[i].type != 0 && partitions[i].size > 0) {
                uint32_t subpartition_offset = partitions[i].IFirst * SECTOR_SIZE + partition_offset;

                fseek(file, subpartition_offset, SEEK_SET);
                fread(buffer, SECTOR_SIZE, 1, file);

                struct partition_table *subpartitions = (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);

                printf("\nSubpartition table (Partition %d):\n", i);
                printf("       ----Start----      ------End-----\n");
                printf("  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");

                for (int j = 0; j < 4; j++) {
                    // Correct CHS interpretation for subpartitions
                    int start_cyl = (subpartitions[j].start_sec & 0xC0) << 2 | subpartitions[j].start_cyl;
                    int end_cyl = (subpartitions[j].end_sec & 0xC0) << 2 | subpartitions[j].end_cyl;
                    printf("  0x%02X    %2d    %2d  %4d 0x%02X    %2d   %2d  %4d %10u %10u\n",
                           subpartitions[j].bootind,
                           subpartitions[j].start_head,
                           subpartitions[j].start_sec & 0x3F,  // Masking the lower 6 bits
                           start_cyl,
                           subpartitions[j].type,
                           subpartitions[j].end_head,
                           subpartitions[j].end_sec & 0x3F,  // Masking the lower 6 bits
                           end_cyl,
                           subpartitions[j].IFirst,
                           subpartitions[j].size);
                }
            }
        }
    }
}




int main(int argc, char *argv[]) {
    int verbose = 0; 
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *path = NULL;
    int i; 
    FILE *file;
    struct superblock sb;

    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    // Parse command-line arguments
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-v") == 0) {
                verbose = 1;
            } else if (strcmp(argv[i], "-p") == 0) {
                if (i + 1 >= argc) {  
                    fprintf(stderr, "error: missing value for -p\n");
                    print_usage();
                    return 1;
                }
                partition = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-s") == 0) {
                if (i + 1 >= argc) {  
                    fprintf(stderr, "error: missing value for -s\n");
                    print_usage();
                    return 1;
                }
                subpartition = atoi(argv[++i]);
            } else {
                fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
                print_usage();
                return 1;
            }
        } else if (imagefile == NULL) {
            imagefile = argv[i];
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "error: too many arguments\n");
            print_usage();
            return 1;
        }
    }

    if (imagefile == NULL) {
        fprintf(stderr, "error: missing image file\n");
        print_usage();
        return 1;
    }

    // Open the image file
    file = fopen(imagefile, "rb"); 
    if (file == NULL) {
        fprintf(stderr, "error: cannot open image file '%s'\n", imagefile);
        return 1;
    }

    // Calculate partition offset
    int partition_offset = 0;  /* default to start of file */
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
        print_partition_table(file, 0, 1);



        if (verbose) {
            printf("Partition %d details:\n", partition);
            printf("  Offset: %d bytes\n", partition_offset);
            read_superblock(file, &sb, partition_offset);
            print_superblock(&sb);
            print_computed_fields(&sb);
        }

        if (subpartition != -1) {
            printf("Subpartition %d details:\n", subpartition);
            print_superblock(&sb);
            printf("  Offset: %d bytes\n", partition_offset);
            print_computed_fields(&sb);
        }
    }



    fclose(file);
    return 0;
}
