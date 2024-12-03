#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minls.h"


#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

 

void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
}

/* read the superblock */
void read_superblock(FILE *file, struct superblock *sb, int partition_offset) {
    fseek(file, partition_offset + 1024, SEEK_SET);
    fread(sb, sizeof(struct superblock), 1, file);
}

/* print verbose superblock info  */
void print_superblock(struct superblock *sb) {
    int zone_size = sb->blocksize * (1 << sb->log_zone_size);
    printf("Superblock Contents:\n");
    printf("Stored Fields:\n");
    printf("  ninodes %u\n", sb->ninodes);
    printf("  i_blocks %d\n", sb->i_blocks);
    printf("  z_blocks %d\n", sb->z_blocks);
    printf("  firstdata %u\n", sb->firstdata);
    printf("  log_zone_size %d (zone size: %d bytes)\n", sb->log_zone_size, 
        zone_size);
    printf("  max_file %u\n", sb->max_file);
    printf("  magic 0x%x\n", sb->magic);
    printf("  zones %u\n", sb->zones);
    printf("  blocksize %u\n", sb->blocksize);
    printf("  subversion %u\n", sb->subversion);
}

/* read an inode by its number */
/* read an inode by its number */
void read_inode(FILE *file, int inode_num, struct inode *inode, struct superblock *sb) {
    // Step 1: Calculate the number of inodes per block
    int inodes_per_block = sb->blocksize / INODE_SIZE;

    // Step 2: Calculate the inode table's start block
    int inode_start_block = 2 + sb->i_blocks + sb->z_blocks; // After inode and zone bitmaps

    // Step 3: Determine which block the inode is in
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block;

    // Step 4: Determine the position of the inode within the block
    int inode_index = (inode_num - 1) % inodes_per_block;

    // Step 5: Calculate the absolute file offset
    long inode_offset = (inode_block * sb->blocksize) + (inode_index * INODE_SIZE);

    printf("Reading inode %d at block %d (start block: %d), index %d, offset %ld\n",
           inode_num, inode_block, inode_start_block, inode_index, inode_offset);

    // Step 6: Seek to the calculated offset
    fseek(file, inode_offset, SEEK_SET);

    // Step 7: Read the raw inode data
    unsigned char raw_inode[INODE_SIZE];
    size_t read_size = fread(raw_inode, 1, INODE_SIZE, file);
    if (read_size != INODE_SIZE) {
        fprintf(stderr, "Error reading inode: expected %d bytes, got %zu\n", INODE_SIZE, read_size);
        memset(inode, 0, sizeof(struct inode)); // Zero out inode to prevent garbage data
        return;
    }

    // Step 8: Debugging output for raw inode data
    printf("\nRaw inode data:\n");
    for (int i = 0; i < INODE_SIZE; i++) {
        printf("%02x ", raw_inode[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }

    // Step 9: Populate the inode structure
    memcpy(inode, raw_inode, sizeof(struct inode));
}

/* list contents of a directory given its inode */
void list_directory(FILE *file, struct inode *dir_inode, struct superblock *sb) {
    char *buffer = malloc(sb->blocksize);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    printf("/:\n");

    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) {
            printf("Zone %d is empty.\n", i);
            continue;
        }

        int max_zone = sb->zones;
        if (dir_inode->zone[i] < 1 || dir_inode->zone[i] > max_zone) {
            printf("Zone[%d] is out of valid range and has been skipped.\n", i);
            continue;
        }

        int block_address = sb->firstdata + (dir_inode->zone[i] - 1);
        printf("Reading block address: %d\n", block_address);

        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, 1, sb->blocksize, file);

        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);
            if (entry->ino != 0) {
                printf("Inode %d, Name %s\n", entry->ino, entry->name);
            }
            offset += sizeof(struct fileent);
        }
    }

    free(buffer);
}


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
    printf("\nFile inode:\n");
    printf("uint16_t mode 0x%x (%s)\n", inode->mode, 
    get_permissions(inode->mode));
    printf("uint16_t links %d\n", inode->links);
    printf("uint16_t uid %d\n", inode->uid);
    printf("uint16_t gid %d\n", inode->gid);
    printf("uint32_t size %u\n", inode->size);
    time_t atime = inode->atime;
    printf("uint32_t atime %u --- %s", inode->atime, ctime(&atime));
    time_t mtime = inode->mtime;
    printf("uint32_t mtime %u --- %s", inode->mtime, ctime(&mtime));
    time_t c_time = inode->c_time;
    printf("uint32_t c_time %u --- %s", inode->c_time, ctime(&c_time));
    printf("\nDirect zones:\n");
    for (int i = 0; i < DIRECT_ZONES; i++) {
        printf("zone[%d] = %u\n", i, inode->zone[i]);
    }
    printf("uint32_t indirect %u\n", inode->indirect);
    printf("uint32_t double %u\n", inode->two_indirect);
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

void read_partition_table(FILE *file, int partition, int subpartition, 
    int *partition_offset) {
    uint8_t buffer[SECTOR_SIZE];

    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    if (buffer[BOOT_SIG_OFFSET] != 0x55 || buffer[BOOT_SIG_OFFSET + 1] != 0xAA) 
    {
        fprintf(stderr, "error: invalid partition table signature\n");
        fclose(file);
        exit(1);
    }

    struct partition_table *partitions = 
        (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];
    if (partition < 0 || partition > 3) {
        fprintf(stderr, "error: invalid primary partition number\n");
        fclose(file);
        exit(1);
    }

    *partition_offset = partitions[partition].IFirst * SECTOR_SIZE;
    if (partitions[partition].type == EXTENDED_PARTITION && subpartition != -1){
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, SECTOR_SIZE, 1, file);
        struct partition_table *subpartitions = 
            (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

        if (subpartition < 0 || subpartition > 3) {
            fprintf(stderr, "error: invalid subpartition number\n");
            fclose(file);
            exit(1);
        }

        if (subpartitions[subpartition].type != PARTITION_TYPE) {
            fprintf(stderr, "error: invalid Minix subpartition type\n");
            fclose(file);
            exit(1);
        }

        *partition_offset = partitions[partition].IFirst * SECTOR_SIZE;
        printf("Subpartition %d: lFirst=%u, size=%u\n", 
               subpartition, 
               subpartitions[subpartition].IFirst, 
               subpartitions[subpartition].size);
    }

    printf("Partition %d: lFirst=%u, size=%u\n", 
           partition, 
           partitions[partition].IFirst, 
           partitions[partition].size);
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
    
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-v") == 0) {
                verbose = 1;
            } else if (strcmp(argv[i], "-p") == 0) {
                if (i + 1 >= argc) {  
                    fprintf(stderr, "error: missing val for -p\n");
                    print_usage();
                    return 1;
                }
                partition = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-s") == 0) {
                if (i + 1 >= argc) {  
                    fprintf(stderr, "error: missing val for -s\n");
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
        fprintf(stderr, "error: missing img file\n");
        print_usage();
        return 1;
    }

    file = fopen(imagefile, "rb"); 
    if (file == NULL) {
        fprintf(stderr, "error: cannot open image file '%s'\n", imagefile);
        return 1;
    }

    int partition_offset = 0;  /* default to start of file */ 
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
        /* adjust fp to start of selected partition*/
        fseek(file, partition_offset, SEEK_SET);  
    }

    read_superblock(file, &sb, partition_offset);

    if (sb.magic != MAGIC_NUM && sb.magic != R_MAGIC_NUM) { 
        fprintf(stderr, "invalid minix filesystem w/ magic number: (0x%x)\n", 
            sb.magic);

        fclose(file);
        return 1;
    }

    if (verbose) {
        print_superblock(&sb);
    }

    struct inode target_inode;
    if (path != NULL) {
        if (find_inode_by_path(file, path, &target_inode, &sb) != 0) {
            fclose(file);
            return 1;
        }

        if (target_inode.mode & DIRECTORY) {
            list_directory(file, &target_inode, &sb);
            print_inode(&target_inode);
        } else {
            list_directory(file, &target_inode, &sb);
            print_inode(&target_inode);
        }
    } else {
        /* if no path, assume root */
        read_inode(file, 1, &target_inode, &sb);
        if (target_inode.mode & DIRECTORY) {
            list_directory(file, &target_inode, &sb);
            print_inode(&target_inode);
        } else {
            list_directory(file, &target_inode, &sb);
            print_inode(&target_inode);
        }
    }

    fclose(file); 
    return 0;
}
