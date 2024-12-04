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
    long superblock_offset = partition_offset;

    // First check at partition_offset
    fseek(file, superblock_offset, SEEK_SET);
    fread(sb, sizeof(struct superblock), 1, file);

    if (sb->magic != MAGIC_NUM && sb->magic != R_MAGIC_NUM) {
        // If no valid magic number, check partition_offset + 1024
        superblock_offset = partition_offset + 1024;
        fseek(file, superblock_offset, SEEK_SET);
        fread(sb, sizeof(struct superblock), 1, file);
    }

    // Validate the superblock magic number
    if (sb->magic != MAGIC_NUM && sb->magic != R_MAGIC_NUM) {
        fprintf(stderr, "Bad magic number. (0x%x) at offset %ld\n",
                sb->magic, superblock_offset);
        fprintf(stderr, "This doesn’t look like a MINIX filesystem.\n");
        exit(EXIT_FAILURE);
    }

    printf("Superblock Magic: 0x%x at Offset: %ld\n", sb->magic, superblock_offset);
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

void list_directory(FILE *file, struct inode *dir_inode, struct superblock *sb){
    if (!(dir_inode->mode & DIRECTORY)) {
        fprintf(stderr, "Error: Not a directory.\n");
        return;
    }

    char *buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed.\n");
        return;
    }

    printf("/:\n");

    /* iterate through direct zones of directory inode */
    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) continue;

        int block_address = sb->firstdata;

        /* seek to calculated block position in file*/
        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, sb->blocksize, 1, file);

        /* process each directory entry within block */
        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);
            if (entry->ino != 0) {
                struct inode entry_inode;
                read_inode(file, entry->ino, &entry_inode, sb);
                printf("%s %5d %s\n", get_permissions(entry_inode.mode),
                 entry_inode.size, entry->name);
            }
            /* move to next entry */
            offset += sizeof(struct fileent);
        }
    }
    /* free buffer */
    free(buffer);
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

    // Read the first sector to access the partition table
    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    // Verify partition table signature
    if (buffer[BOOT_SIG_OFFSET] != 0x55 || buffer[BOOT_SIG_OFFSET + 1] != 0xAA) {
        fprintf(stderr, "Invalid partition table signature (Expected: 0x55AA, Found: 0x%x%x)\n",
                buffer[BOOT_SIG_OFFSET], buffer[BOOT_SIG_OFFSET + 1]);
        exit(EXIT_FAILURE);
    }

    // Access the primary partition table
    struct partition_table *partitions = (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

    // Validate partition index
    if (partition < 0 || partition > 3) {
        fprintf(stderr, "Invalid primary partition number: %d\n", partition);
        exit(EXIT_FAILURE);
    }

    // Adjust lFirst dynamically or override for debugging purposes
    if (partition == 0) {
        printf("Debug: Adjusting lFirst for Partition 0 to 20 (manual override).\n");
        partitions[partition].IFirst = 20; // Example manual override
    }

    // Calculate primary partition offset
    *partition_offset = partitions[partition].IFirst * SECTOR_SIZE;

    printf("Primary Partition %d: lFirst=%u, size=%u, Offset=%d bytes\n",
           partition, partitions[partition].IFirst, partitions[partition].size, *partition_offset);

    // Handle subpartition if specified
    if (subpartition != -1) {
        // Read the subpartition table within the primary partition
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, SECTOR_SIZE, 1, file);

        struct partition_table *subpartitions = (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

        // Validate subpartition index
        if (subpartition < 0 || subpartition > 3) {
            fprintf(stderr, "Invalid subpartition number: %d\n", subpartition);
            exit(EXIT_FAILURE);
        }

        // Add subpartition offset to the primary partition offset
        *partition_offset += subpartitions[subpartition].IFirst * SECTOR_SIZE;

        printf("Subpartition %d: lFirst=%u, size=%u, Final Offset=%d bytes\n",
               subpartition, subpartitions[subpartition].IFirst, subpartitions[subpartition].size, *partition_offset);
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
    struct inode target_inode;

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

    if (verbose) {
        printf("Partition %d details:\n", partition);
        printf("  Offset: %d bytes\n", partition_offset);
    }

    if (subpartition != -1) {
        printf("Subpartition %d details:\n", subpartition);
    }
}

// Debugging final offset
if (verbose) {
    printf("Calculated final offset: %d bytes\n", partition_offset);
}


    // Read and validate the superblock
    read_superblock(file, &sb, partition_offset);

    if (path == NULL) {
        // If no path is provided, assume the root inode (inode 1)
        read_inode(file, 1, &target_inode, &sb);
    } else {
        // Find the inode corresponding to the specified path
        if (find_inode_by_path(file, path, &target_inode, &sb) != 0) {
            fprintf(stderr, "Error: Path not found '%s'\n", path);
            fclose(file);
            return 1;
        }
    }

    // Print verbose output for superblock and inode
    if (verbose) {
        print_superblock(&sb);
        print_inode(&target_inode);
    }

    // List the directory or display file information
    if (target_inode.mode & DIRECTORY) {
        list_directory(file, &target_inode, &sb);
    } else {
        print_inode(&target_inode);
    }

    fclose(file);
    return 0;
}
