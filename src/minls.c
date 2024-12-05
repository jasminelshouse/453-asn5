#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minls.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
}

void read_superblock(FILE *file, struct superblock *sb, int partition_offset, int verbose) {
    int possible_offsets[] = {0, 1024, 1536, 2048}; // Adjusted to include 0 for subpartition start
    int num_offsets = sizeof(possible_offsets) / sizeof(possible_offsets[0]);
    long superblock_offset;
    int valid_superblock_found = 0;

    if (verbose) {
        printf("DEBUG: Starting superblock read. Partition offset: %d\n", partition_offset);
    }

    for (int i = 0; i < num_offsets; i++) {
        superblock_offset = partition_offset + possible_offsets[i];

        if (verbose) {
            printf("DEBUG: Trying superblock offset: %ld\n", superblock_offset);
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
                printf("DEBUG: Valid superblock found at offset: %ld\n", superblock_offset);
            }
            break;
        } else {
            if (verbose) {
                printf("DEBUG: Invalid magic number: 0x%x at offset: %ld\n", sb->magic, superblock_offset);
            }
        }
    }

    if (!valid_superblock_found) {
        fprintf(stderr, "ERROR: Failed to locate a valid superblock.\n");
        exit(EXIT_FAILURE);
    }
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

void read_inode(FILE *file, int inode_num, struct inode *inode, struct superblock *sb) {
    int verbose = 1;

    // Validate the inode number is within bounds
    if (inode_num < 1 || inode_num > sb->ninodes) {
        fprintf(stderr, "Error: inode number %d is out of bounds (1-%u)\n", inode_num, sb->ninodes);
        return;
    }

    // Calculate inode-related values
    int inodes_per_block = sb->blocksize / INODE_SIZE;
    int inode_start_block = 2 + sb->i_blocks + sb->z_blocks;
// Start block for inode table
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block; 
    fprintf(stderr, "DEBUG: inode_block=%d (inode_num=%d, inode_start_block=%d, inodes_per_block=%d)\n",
        inode_block, inode_num, inode_start_block, inodes_per_block);// Block containing the inode
    int inode_index = (inode_num - 1) % inodes_per_block; // Index of the inode within the block
    printf("inode index: %d\n", inode_index);
    long inode_offset = (inode_block * sb->blocksize) + (inode_index * INODE_SIZE); // Byte offset to the inode
    fprintf(stderr, "DEBUG: Calculated inode_offset=%ld (block_start=%ld, inode_position=%ld)\n",
        inode_offset,
        inode_block * sb->blocksize,
        inode_index * INODE_SIZE);
    // Debugging: Output inode-related calculations
    if (verbose) {
        printf("DEBUG: Starting read_inode for inode_num=%d\n", inode_num);
        printf("DEBUG: Superblock i_blocks=%d, z_blocks=%d, blocksize=%d\n",
               sb->i_blocks, sb->z_blocks, sb->blocksize);
        printf("DEBUG: inodes_per_block=%d\n", inodes_per_block);
        printf("DEBUG: inode_start_block=%d (should be > i_blocks + z_blocks)\n", inode_start_block);
        printf("DEBUG: inode_block=%d, inode_index=%d\n", inode_block, inode_index);
        printf("DEBUG: Calculated inode_offset=%ld (block=%d, index=%d)\n", inode_offset, inode_block, inode_index);
        printf("DEBUG: inode size=%d\n", INODE_SIZE);
    }


    // Seek to the inode position
    if (fseek(file, inode_offset, SEEK_SET) != 0) {
        perror("Failed to seek to inode position");
        return;
    }

    // Read raw inode data
    unsigned char raw_inode[INODE_SIZE];
    if (fread(raw_inode, INODE_SIZE, 1, file) != 1) {
        perror("Failed to read inode from disk");
        return;
    }

    // Debugging: Output raw inode data
    if (verbose) {
        printf("DEBUG: Raw inode data:\n");
        for (int i = 0; i < INODE_SIZE; i++) {
            printf("%02x ", raw_inode[i]);
            if ((i + 1) % 16 == 0) {
                printf("\n");
            }
        }
        printf("\n");
    }

    // Copy the raw data into the inode structure
    memset(inode, 0, sizeof(struct inode));
    memcpy(inode, raw_inode, INODE_SIZE);

    // Debugging: Output inode fields
    if (verbose) {
        printf("DEBUG: Inode fields:\n");
        printf("  Mode: 0x%x\n", inode->mode);
        printf("  Links: %d\n", inode->links);
        printf("  UID: %d\n", inode->uid);
        printf("  GID: %d\n", inode->gid);
        printf("  Size: %u bytes\n", inode->size);
        printf("  Direct zones:\n");
        for (int i = 0; i < DIRECT_ZONES; i++) {
            printf("    zone[%d]: %u\n", i, inode->zone[i]);
        }
        printf("  Indirect zone: %u\n", inode->indirect);
        printf("  Double indirect zone: %u\n", inode->two_indirect);
    }

     if ((inode->mode & 0xF000) == 0x6000) {  // Check if it's a block special file
        printf("DEBUG: Inode %d is a block special file.\n", inode_num);

        // Use zone[0] to find the starting block of the subpartition
        if (inode->zone[0] != 0) {
            long subpartition_offset = inode->zone[0] * sb->blocksize;
            printf("DEBUG: Subpartition starts at offset %ld bytes.\n", subpartition_offset);
            printf("firstdata: %d\n", sb->firstdata);
            printf("subpartition zone: %d\n", inode->zone[0]);
            printf("block size %d\n", sb->blocksize);

            // Optionally: Read and validate the superblock of the subpartition
            struct superblock subpartition_sb;
            read_superblock(file, &subpartition_sb, subpartition_offset, 1);  // 1 = verbose mode
            print_superblock(&subpartition_sb);
        } else {
            fprintf(stderr, "ERROR: Block special file has no valid zone pointer.\n");
        }
    }
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
    if (!(dir_inode->mode & DIRECTORY)) {
        fprintf(stderr, "Error: Inode is not a directory.\n");
        return;
    }

    char *buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return;
    }

    printf("/:\n");

    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) {
            fprintf(stderr, "DEBUG: Zone %d is unused or uninitialized.\n", i);
            continue;
        }

        int block_address = sb->firstdata + (dir_inode->zone[i] - 1);
        printf("DEBUG: Reading zone %d at block address %d\n", i, block_address);

        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, sb->blocksize, 1, file);

        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);
            if (entry->ino == 0) {
                offset += DIRECTORY_ENTRY_SIZE;
                continue;
            }

            if (entry->ino < 1 || entry->ino > sb->ninodes) {
                fprintf(stderr, "DEBUG: Invalid inode number: %d\n", entry->ino);
                offset += DIRECTORY_ENTRY_SIZE;
                continue;
            }

            struct inode entry_inode;
            read_inode(file, entry->ino, &entry_inode, sb);

            if ((entry_inode.mode & 0xF000) == 0x6000) {
                printf("DEBUG: Entry '%s' is a block special file.\n", entry->name);
                long subpartition_offset = entry_inode.zone[0] * sb->blocksize;
                printf("subpartition zone: %d\n", entry_inode.zone[0]);
                printf("block size %d\n", sb->blocksize);
                printf("DEBUG: Subpartition starts at offset %ld bytes.\n", subpartition_offset);

                // Read and validate subpartition superblock
                struct superblock subpartition_sb;
                read_superblock(file, &subpartition_sb, subpartition_offset, 1);
                print_superblock(&subpartition_sb);

                // Optionally traverse into the subpartition
            }


            printf("%s %5d %s\n",
                   get_permissions(entry_inode.mode),
                   entry_inode.size,
                   entry->name);

            offset += DIRECTORY_ENTRY_SIZE;
        }
    }

    free(buffer);
}




int traverse_directory(FILE *file, struct inode *current_inode,
                       const char *entry_name, struct inode *found_inode, struct superblock *sb) {
    char *buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return 0;
    }

    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (current_inode->zone[i] == 0) {
            printf("DEBUG: Zone %d is empty, skipping.\n", i);
            continue;
        }

        // Calculate block address
        int block_address = sb->firstdata + (current_inode->zone[i] - 1);
        if (block_address < sb->firstdata) {
            fprintf(stderr, "Error: Invalid block address %d in zone %d\n", block_address, i);
            continue;
        }

        // Read directory block
        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, sb->blocksize, 1, file);

        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);

            if (entry->ino == 0) {  // Skip invalid entries
                offset += sizeof(struct fileent);
                continue;
            }

            printf("DEBUG: Directory entry: name='%s', ino=%d\n", entry->name, entry->ino);

            if (strcmp(entry->name, entry_name) == 0) {
                // Found the target entry
                read_inode(file, entry->ino, found_inode, sb);
                free(buffer);
                return 1;  // Found
            }

            offset += sizeof(struct fileent);
        }
    }

    free(buffer);
    return 0;  // Not found
}




void print_inode(struct inode *inode) {
    printf("\nFile inode:\n");
    printf("  unsigned short mode       0x%x    (%s)\n", inode->mode, get_permissions(inode->mode));
    printf("  unsigned short links         %d\n", inode->links);
    printf("  unsigned short uid           %d\n", inode->uid);
    printf("  unsigned short gid           %d\n", inode->gid);
    printf("  uint32_t size                %u\n", inode->size);

    // Safely handle packed members
    time_t access_time = inode->atime;
    time_t mod_time = inode->mtime;
    time_t change_time = inode->c_time;  // Use correct member name

    printf("  uint32_t atime     %u    --- %s", inode->atime, ctime(&access_time));
    printf("  uint32_t mtime     %u    --- %s", inode->mtime, ctime(&mod_time));
    printf("  uint32_t ctime     %u    --- %s", inode->c_time, ctime(&change_time));

    printf("\nDirect zones:\n");
    for (int i = 0; i < DIRECT_ZONES; i++) {
        printf("  zone[%d]   = %u\n", i, inode->zone[i]);
    }
    printf("  uint32_t indirect   = %u\n", inode->indirect);
    printf("  uint32_t double     = %u\n", inode->two_indirect);
}


int find_inode_by_path(FILE *file, const char *path, struct inode *inode, struct superblock *sb) {
    if (strcmp(path, "/") == 0) {
        read_inode(file, 1, inode, sb);  // Root inode
        return 0;
    }

    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    struct inode current_inode;
    read_inode(file, 1, &current_inode, sb);  // Start at root

    while (token != NULL) {
        if (!(current_inode.mode & DIRECTORY)) {
            fprintf(stderr, "Error: '%s' is not a directory.\n", token);
            free(path_copy);
            return -1;
        }

        if (!traverse_directory(file, &current_inode, token, &current_inode, sb)) {
            fprintf(stderr, "Error: Path component '%s' not found.\n", token);
            free(path_copy);
            return -1;
        }

        token = strtok(NULL, "/");
    }

    *inode = current_inode;  // Final inode
    free(path_copy);
    return 0;
}


void read_partition_table(FILE *file, int partition, int subpartition, int *partition_offset) {
    uint8_t buffer[SECTOR_SIZE];
    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    // Validate partition table signature
    if (buffer[BOOT_SIG_OFFSET] != 0x55 || buffer[BOOT_SIG_OFFSET + 1] != 0xAA) {
        fprintf(stderr, "Invalid partition table signature (Expected: 0x55AA, Found: 0x%02X%02X)\n",
                buffer[BOOT_SIG_OFFSET], buffer[BOOT_SIG_OFFSET + 1]);
        exit(EXIT_FAILURE);
    }

    struct partition_table *partitions = (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

    // Validate primary partition index
    if (partition < 0 || partition >= 4) {
        fprintf(stderr, "Invalid primary partition number: %d\n", partition);
        exit(EXIT_FAILURE);
    }

    // Get primary partition info
    uint32_t primary_sector = partitions[partition].IFirst;
    *partition_offset = primary_sector * SECTOR_SIZE;

    printf("Primary Partition %d: IFirst=%u, size=%u, Offset=%d bytes\n",
           partition, primary_sector, partitions[partition].size, *partition_offset);

    if (partitions[partition].type != 0x81) {
        fprintf(stderr, "Partition %d is not a Minix partition (type=0x%02X).\n", partition, partitions[partition].type);
        exit(EXIT_FAILURE);
    }

    // Handle subpartition table
    if (subpartition != -1) {
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, SECTOR_SIZE, 1, file);

        struct partition_table *subpartitions = (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

        if (subpartition < 0 || subpartition >= 4) {
            fprintf(stderr, "Invalid subpartition number: %d\n", subpartition);
            exit(EXIT_FAILURE);
        }

        uint32_t sub_sector = subpartitions[subpartition].IFirst;
        int subpartition_offset = sub_sector * SECTOR_SIZE;

        printf("Subpartition %d: IFirst=%u, size=%u, Offset=%d bytes\n",
               subpartition, sub_sector, subpartitions[subpartition].size, subpartition_offset);

        *partition_offset = subpartition_offset;
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
    FILE *file;
    struct superblock sb;
    struct inode target_inode;

    // Parse command-line arguments
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
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
        } else if (!path) {
            path = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage();
            return EXIT_FAILURE;
        }
    }

    if (!imagefile) {
        fprintf(stderr, "Error: Missing image file\n");
        print_usage();
        return EXIT_FAILURE;
    }

    // Open the image file
    file = fopen(imagefile, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open image file '%s'\n", imagefile);
        return EXIT_FAILURE;
    }

    // Read partition and subpartition details
    int partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);

        if (verbose) {
            printf("DEBUG: Partition offset = %d bytes\n", partition_offset);
            print_partition_table(file, 0, 1);
        }
    }

    // Read the superblock
    read_superblock(file, &sb, partition_offset, verbose);
    if (verbose) {
        print_superblock(&sb);
        print_computed_fields(&sb);
    }

    // Determine the target inode (root or specified path)
    if (!path) {
        read_inode(file, 1, &target_inode, &sb); // Default to root inode
    } else {
        if (find_inode_by_path(file, path, &target_inode, &sb) != 0) {
            fprintf(stderr, "Error: Path not found '%s'\n", path);
            fclose(file);
            return EXIT_FAILURE;
        }
    }

    // Print details for verbose mode
    if (verbose) {
        print_inode(&target_inode);
    }

    // Handle target inode
    if (target_inode.mode & DIRECTORY) {
        list_directory(file, &target_inode, &sb);
    } else {
        print_inode(&target_inode);
    }

    fclose(file);
    return EXIT_SUCCESS;
}
