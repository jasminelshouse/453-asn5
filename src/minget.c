#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minget.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

/* function declarations */
void read_partition_table(FILE *file, int partition, int subpartition, 
    int *partition_offset);
void read_superblock(FILE *file, struct superblock *sb, int partition_offset, 
    int verbose);
int find_inode_by_path(FILE *file, const char *path, struct inode *inode, 
    struct superblock *sb, int partition_offset);
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


void read_superblock(FILE *file, struct superblock *sb, int partition_offset,
                     int verbose)
{
    // Common superblock offsets
    int possible_offsets[] = {1024, 1536, 2048};
    int num_offsets = sizeof(possible_offsets) / sizeof(possible_offsets[0]);
    long superblock_offset;
    int valid_superblock_found = 0;
    int i;

    for (i = 0; i < num_offsets; i++)
    {
        superblock_offset = partition_offset + possible_offsets[i];

        if (fseek(file, superblock_offset, SEEK_SET) != 0)
        {
            perror("Failed to seek to superblock");
            continue;
        }

        if (fread(sb, sizeof(struct superblock), 1, file) != 1)
        {
            perror("Failed to read superblock");
            continue;
        }

        if (sb->magic == MAGIC_NUM || sb->magic == MAGIC_NUM_OLD ||
            sb->magic == R_MAGIC_NUM || sb->magic == R_MAGIC_NUM_OLD)
        {
            valid_superblock_found = 1;
            if (verbose)
            {
            }
            break;
        }
    }

    if (sb->magic != MAGIC_NUM && sb->magic != R_MAGIC_NUM)
    {
        /* error message if not valid */
        fprintf(stderr, "Bad magic number. (0x%x)\nThis doesn’t look like "
                        "a MINIX filesystem.\n",
                sb->magic);
        exit(EXIT_FAILURE);
    }

    if (!valid_superblock_found)
    {
        fprintf(stderr, "Failed to locate a valid superblock.\n");
        exit(EXIT_FAILURE);
    }
}


void read_inode(FILE *file, int inode_num, struct inode *inode,
                struct superblock *sb, int partition_offset)
{
    int inodes_per_block = sb->blocksize / INODE_SIZE;
    int inode_start_block = 2 + sb->i_blocks + sb->z_blocks;
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block;
    int inode_index = (inode_num - 1) % inodes_per_block;
    unsigned char raw_inode[INODE_SIZE];

    // Include the partition offset in the inode offset calculation
    long inode_offset = partition_offset +
                        (inode_block * sb->blocksize) +
                        (inode_index * INODE_SIZE);

    
    if (fseek(file, inode_offset, SEEK_SET) != 0)
    {
        perror("Failed to seek to inode");
        return;
    }

    
    if (fread(raw_inode, INODE_SIZE, 1, file) != 1)
    {
        perror("Failed to read inode");
        return;
    }

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
    const char *entry_name, struct inode *found_inode, struct superblock *sb)
{
    char buffer[4096];
    struct fileent *entry;
    int i, offset;

    for (i = 0; i < DIRECT_ZONES; i++)
    {
        if (current_inode->zone[i] == 0)
            continue;

        int block_address = sb->firstdata + (current_inode->zone[i] - 1);
        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, 1, sb->blocksize, file);
        offset = 0;

        while (offset < sb->blocksize)
        {
            entry = (struct fileent *)(buffer + offset);
            if (entry->ino != 0 && strcmp(entry->name, entry_name) == 0)
            {
                read_inode(file, entry->ino, found_inode, sb, offset);
                return 1; /* found */
            }
            offset += sizeof(struct fileent);
        }
    }
    return 0; /* not found */
}



int find_inode_by_path(FILE *file, const char *path, struct inode *inode,
                       struct superblock *sb, int partition_offset)
{
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    struct inode current_inode;

    if (strcmp(path, "/") == 0)
    {
        // Read the root inode with partition_offset
        read_inode(file, 1, inode, sb, partition_offset);
        free(path_copy);
        return 0;
    }

    // Start at the root inode
    read_inode(file, 1, &current_inode, sb, partition_offset);

    while (token != NULL)
    {
        // Check if the current inode is a directory
        if (!S_ISDIR(current_inode.mode))
        {
            fprintf(stderr, "Not a directory\n");
            free(path_copy);
            return -1;
        }

        printf("Resolving token: %s\n", token);

        // Traverse the directory to find the next inode
        if (!traverse_directory(file, &current_inode, token,
                                &current_inode, sb))
        {
            fprintf(stderr, "Path not found: %s\n", token);
            free(path_copy);
            return -1;
        }

        token = strtok(NULL, "/");
    }

    *inode = current_inode; // Found the inode of the last token
    free(path_copy);
    return 0;
}

void read_partition_table(FILE *file, int partition, int subpartition,
                          int *partition_offset)
{
    uint8_t buffer[SECTOR_SIZE];
    // Parse the primary partition table
    struct partition_table *partitions =
        (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

    // Read the MBR (Master Boot Record)
    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    // Validate MBR signature
    if (buffer[BOOT_SIG_OFFSET] != 0x55 || buffer[BOOT_SIG_OFFSET + 1] != 0xAA)
    {
        fprintf(stderr, "Error: Invalid partition table signature\n");
        fclose(file);
        exit(1);
    }

    if (partition < 0 || partition > 3)
    {
        fprintf(stderr, "Error: Invalid primary partition number\n");
        fclose(file);
        exit(1);
    }

    // Validate the primary partition type
    if (partitions[partition].type == 0 || partitions[partition].size == 0)
    {
        fprintf(stderr, "Error: Partition %d is empty or invalid\n", partition);
        fclose(file);
        exit(1);
    }

    // Calculate primary partition offset
    *partition_offset = partitions[partition].IFirst * SECTOR_SIZE;

    
    // If a subpartition is specified, handle extended partitions


    if (partitions[partition].type == EXTENDED_PARTITION && subpartition != -1)
    {

        // Read the extended partition table
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, SECTOR_SIZE, 1, file);

        struct partition_table *subpartitions =
            (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

        if (subpartition < 0 || subpartition > 3)
        {
            fprintf(stderr, "Error: Invalid subpartition number\n");
            fclose(file);
            exit(1);
        }

        // Validate the subpartition type and size
        if (subpartitions[subpartition].type == 0 || 
            subpartitions[subpartition].size == 0)
        {
            fprintf(stderr, "Error: Subpartition %d is empty or invalid\n", 
                subpartition);
            fclose(file);
            exit(1);
        }

        // Add the subpartition offset to the primary partition offset
        *partition_offset += subpartitions[subpartition].IFirst * SECTOR_SIZE;

        printf("Subpartition %d: lFirst=%u, size=%u\n",
               subpartition,
               subpartitions[subpartition].IFirst,
               subpartitions[subpartition].size);


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
    int partition_offset = 0;

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
    partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
    }

    /* read superblock of selected partition */
    read_superblock(file, &sb, partition_offset, verbose);

    /* find target inode based on the provided path */
    if (find_inode_by_path(file, srcpath, &src_inode, &sb, 
        partition_offset) != 0) {
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
