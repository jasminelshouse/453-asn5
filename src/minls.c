#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minls.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

void print_usage()
{
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
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

    for (int i = 0; i < num_offsets; i++)
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

/* self explanatory */
void print_superblock(struct superblock *sb)
{
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

void print_computed_fields(struct superblock *sb)
{
    int zone_size = sb->blocksize * (1 << sb->log_zone_size);
    int ptrs_per_zone = zone_size / sizeof(uint32_t);
    int ino_per_block = sb->blocksize / INODE_SIZE;
    int fileent_size = DIRECTORY_ENTRY_SIZE;
    int ent_per_zone = zone_size / fileent_size;

    // Compute version based on magic number
    int version =
        (sb->magic == MAGIC_NUM) ? 3 : (sb->magic == MAGIC_NUM_OLD) ? 2
                                                                    : 0;

    // First inode map, zone map, and inode block
    int firstImap = 2; // Typically starts at block 2
    int firstZmap = firstImap + sb->i_blocks;
    int firstIblock = firstZmap + sb->z_blocks;

    // wrongended (check for byte-swapped magic number)
    int wrongended = 
        (sb->magic == R_MAGIC_NUM || sb->magic == R_MAGIC_NUM_OLD) ? 1 : 0;

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

void read_inode(FILE *file, int inode_num, struct inode *inode,
                struct superblock *sb, int partition_offset)
{
    int inodes_per_block = sb->blocksize / INODE_SIZE;
    int inode_start_block = 2 + sb->i_blocks + sb->z_blocks;
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block;
    int inode_index = (inode_num - 1) % inodes_per_block;

    // Include the partition offset in the inode offset calculation
    long inode_offset = partition_offset +
                        (inode_block * sb->blocksize) +
                        (inode_index * INODE_SIZE);

    if (fseek(file, inode_offset, SEEK_SET) != 0)
    {
        perror("Failed to seek to inode");
        return;
    }

    unsigned char raw_inode[INODE_SIZE];
    if (fread(raw_inode, INODE_SIZE, 1, file) != 1)
    {
        perror("Failed to read inode");
        return;
    }

    memcpy(inode, raw_inode, sizeof(struct inode));
}

/* self explanatory */
const char *get_permissions(uint16_t mode)
{
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

void list_directory(FILE *file, struct inode *dir_inode,
                    struct superblock *sb, int partition_offset) {
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

    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) {
            printf("Empty zone %d\n", i);
            continue; // Skip empty zones
        }

        // Calculate block address directly from the zone
        long block_address = partition_offset + 
                             ((long)(dir_inode->zone[i]) * sb->blocksize);

        printf("Zone %d: dir_inode->zone[%d] = %u, block_address = %ld\n",
               i, i, dir_inode->zone[i], block_address);

        if (fseek(file, block_address, SEEK_SET) != 0) {
            perror("Failed to seek to block position");
            continue;
        }

        if (fread(buffer, sb->blocksize, 1, file) != 1) {
            perror("Failed to read block data");
            continue;
        }

        int offset = 0;
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);
            printf("entry->ino: %d, entry->name: %s\n", entry->ino, entry->name);

            if (entry->ino != 0 && strlen(entry->name) > 0) {
                struct inode entry_inode;

                read_inode(file, entry->ino, &entry_inode, sb, partition_offset);

                printf("%s %5d %s\n",
                       get_permissions(entry_inode.mode),  // Permissions
                       entry_inode.size,                   // File size
                       entry->name);                       // File name
            }

            offset += sizeof(struct fileent);
        }
    }

    free(buffer);
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

void print_inode(struct inode *inode)
{
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
    for (i = 0; i < DIRECT_ZONES; i++)
    {
        printf("  zone[%d] = %u\n", i, inode->zone[i]);
    }
    printf("uint32_t indirect %u\n", inode->indirect);
    printf("uint32_t double %u\n", inode->two_indirect);
    printf("\n");
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

    // Parse the primary partition table
    struct partition_table *partitions =
        (struct partition_table *)&buffer[PARTITION_TABLE_OFFSET];

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

void print_partition_table(FILE *file, int partition_offset, 
    int print_subpartitions)
{
    uint8_t buffer[SECTOR_SIZE];

    // Seek to the partition offset
    fseek(file, 0, SEEK_SET);
    fread(buffer, SECTOR_SIZE, 1, file);

    struct partition_table *partitions =
        (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);

    printf("Partition table:\n");
    printf("       ----Start----      ------End-----\n");
    printf("  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");

    for (int i = 0; i < 4; i++)
    {
        // Decode CHS values for start and end
        int start_cyl = 
        ((partitions[i].start_sec & 0xC0) << 2) | partitions[i].start_cyl;
        int end_cyl = 
        ((partitions[i].end_sec & 0xC0) << 2) | partitions[i].end_cyl;

    printf("  0x%02X    %2d    %2d  %4d  0x%02X    %2d   %2d  %4d %10u %10u\n",
            partitions[i].bootind,
            partitions[i].start_head,
            partitions[i].start_sec & 0x3F, // Mask to get sector number
            start_cyl,
            partitions[i].type,
            partitions[i].end_head,
            partitions[i].end_sec & 0x3F, // Mask to get sector number
            end_cyl,
            partitions[i].IFirst,
            partitions[i].size);
}

    // Handle subpartitions if requested
    if (print_subpartitions)
    {
        for (int i = 0; i < 4; i++)
        {
            if (partitions[i].type != 0 && partitions[i].size > 1)
            {
                uint32_t subpartition_offset =
                    (partitions[i].IFirst * SECTOR_SIZE) + partition_offset;

                // Read the subpartition table
                fseek(file, partition_offset, SEEK_SET);
                fread(buffer, SECTOR_SIZE, 1, file);

                struct partition_table *subpartitions =
                    (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);

                printf("\nSubpartition table:\n", i);
                printf("       ----Start----      ------End-----\n");
    printf("  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");

for (int j = 0; j < 4; j++)
{
    int start_cyl =
        ((subpartitions[j].start_sec & 0xC0) << 2) | subpartitions[j].start_cyl;
    int end_cyl =
        ((subpartitions[j].end_sec & 0xC0) << 2) | subpartitions[j].end_cyl;

    printf("  0x%02X    %2d    %2d  %4d  0x%02X    %2d   %2d  %4d %10u %10u\n",
            subpartitions[j].bootind,
            subpartitions[j].start_head,
            subpartitions[j].start_sec & 0x3F,
            start_cyl,
            subpartitions[j].type,
            subpartitions[j].end_head,
            subpartitions[j].end_sec & 0x3F,
            end_cyl,
            subpartitions[j].IFirst,
            subpartitions[j].size);
}
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int verbose = 0;
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *path = NULL;
    int i;
    FILE *file;
    struct superblock sb;
    struct inode target_inode;
    int partition_offset = 0;

    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (strcmp(argv[i], "-v") == 0)
            {
                verbose = 1;
            }
            else if (strcmp(argv[i], "-p") == 0)
            {
                if (i + 1 >= argc)
                {
                    fprintf(stderr, "error: missing value for -p\n");
                    print_usage();
                    return 1;
                }
                partition = atoi(argv[++i]);
            }
            else if (strcmp(argv[i], "-s") == 0)
            {
                if (i + 1 >= argc)
                {
                    fprintf(stderr, "error: missing value for -s\n");
                    print_usage();
                    return 1;
                }
                subpartition = atoi(argv[++i]);
            }
            else
            {
                fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
                print_usage();
                return 1;
            }
        }
        else if (imagefile == NULL)
        {
            imagefile = argv[i];
        }
        else if (path == NULL)
        {
            path = argv[i];
        }
        else
        {
            fprintf(stderr, "error: too many arguments\n");
            print_usage();
            return 1;
        }
    }

    if (imagefile == NULL)
    {
        fprintf(stderr, "error: missing image file\n");
        print_usage();
        return 1;
    }

    file = fopen(imagefile, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "error: cannot open image file '%s'\n", imagefile);
        return 1;
    }

    if (partition != -1)
    {
        read_partition_table(file, partition, subpartition, &partition_offset);

        if (verbose)
        {
            printf("Partition %d: offset=%d\n", partition, partition_offset);
            print_partition_table(file, partition_offset, subpartition != -1);
        }
    }
    else
    {
        partition_offset = 0;
    }

    read_superblock(file, &sb, partition_offset, 1);

    if (path == NULL)
    {
        /* if no path, assume root */
        read_inode(file, 1, &target_inode, &sb, partition_offset);
    }
    else
    {
        /* find inode by specified path */
        if (find_inode_by_path(file, path, &target_inode, &sb, 
        partition_offset) != 0)
        {
            fprintf(stderr, "Error: Path not found '%s'\n", path);
            fclose(file);
            return 1;
        }
    }

    if (verbose)
    {
        print_superblock(&sb);
        print_computed_fields(&sb);
        print_inode(&target_inode);
    }

    if (target_inode.mode & DIRECTORY)
    {
        list_directory(file, &target_inode, &sb, partition_offset);
    }
    else
    {
        print_inode(&target_inode);
    }

    fclose(file);
    return 0;
}