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

int read_inode(FILE *file, int inode_num, struct inode *inode,
               struct superblock *sb, int partition_offset) {
    int inodes_per_block = sb->blocksize / INODE_SIZE;
    int inode_start_block = 2 + sb->i_blocks + sb->z_blocks;
    int inode_block = ((inode_num - 1) / inodes_per_block) + inode_start_block;
    int inode_index = (inode_num - 1) % inodes_per_block;
    unsigned char raw_inode[INODE_SIZE];

    long inode_offset = partition_offset +
                        (inode_block * sb->blocksize) +
                        (inode_index * INODE_SIZE);

    if (fseek(file, inode_offset, SEEK_SET) != 0) {
        perror("Failed to seek to inode");
        return -1;  // Indicate failure
    }

    if (fread(raw_inode, INODE_SIZE, 1, file) != 1) {
        perror("Failed to read inode");
        return -1;  // Indicate failure
    }

    memcpy(inode, raw_inode, sizeof(struct inode));
    return 0;  // Indicate success
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
    
    // Calculate the zone size
    long zonesize = sb->blocksize << sb->log_zone_size;
    char *buffer = malloc(zonesize);  // Allocate for the entire zone size

    if (!(dir_inode->mode & DIRECTORY)) {
        fprintf(stderr, "Error: Not a directory.\n");
        return;
    }
   
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed.\n");
        return;
    }

    for (int i = 0; i < DIRECT_ZONES; i++) {
        // Correctly calculate block address using zone size
        long block_address = partition_offset + 
                             ((long)(dir_inode->zone[i]) * zonesize);
        int offset = 0;
        struct inode entry_inode;

        if (dir_inode->zone[i] == 0) {
            continue; // Skip empty zones
        }

        // printf("Zone %d: dir_inode->zone[%d] = %u, block_address = %ld\n",
        //        i, i, dir_inode->zone[i], block_address);

        if (fseek(file, block_address, SEEK_SET) != 0) {
            perror("Failed to seek to block position");
            continue;
        }

        if (fread(buffer, zonesize, 1, file) != 1) {
            perror("Failed to read block data");
            continue;
        }

        while (offset < zonesize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);

            if (entry->ino != 0 && strlen(entry->name) > 0) {

                // Read inode data for each directory entry
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
                       const char *entry_name, struct inode *found_inode,
                       struct superblock *sb, int partition_offset) {
    size_t zone_size = sb->blocksize * (1 << sb->log_zone_size);
    char *buffer = malloc(zone_size);
    if (!buffer) {
        fprintf(stderr, 
        "Error: Failed to allocate memory for directory buffer\n");
        return 0;
    }

    for (int i = 0; i < DIRECT_ZONES; i++) {
        if (current_inode->zone[i] == 0) {
            continue;  // Skip empty zones
        }

        long block_address = partition_offset + 
        (current_inode->zone[i] * sb->blocksize);

        if (fseek(file, block_address, SEEK_SET) != 0) {
            perror("Error seeking to directory zone");
            free(buffer);
            return 0;
        }

        if (fread(buffer, zone_size, 1, file) != 1) {
            perror("Error reading directory zone");
            free(buffer);
            return 0;
        }

        for (size_t offset = 0; 
        offset < zone_size; offset += DIRECTORY_ENTRY_SIZE) {
            struct fileent *entry = (struct fileent *)(buffer + offset);

            if (entry->ino != 0 && strcmp(entry->name, entry_name) == 0) {
                if (read_inode(file, entry->ino, found_inode, sb, 
                partition_offset) != 0) {
                    fprintf(stderr, "Error: Failed to read inode for '%s'\n",
                     entry_name);
                    free(buffer);
                    return 0;
                }

                free(buffer);
                return 1;  // Entry found
            }
        }
    }

    free(buffer);
    return 0;  // Entry not found
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
                       struct superblock *sb, int partition_offset) {
    if (!path || path[0] != '/') {
        fprintf(stderr, "Error: Path must be absolute\n");
        return -1;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        perror("Error duplicating path");
        return -1;
    }

    char *token, *saveptr;
    token = strtok_r(path_copy, "/", &saveptr);

    struct inode current_inode;
    if (read_inode(file, 1, &current_inode, sb, partition_offset) != 0) {
        fprintf(stderr, "Error: Failed to read root inode\n");
        free(path_copy);
        return -1;
    }

    while (token != NULL) {
        if (!S_ISDIR(current_inode.mode)) {
            fprintf(stderr, "Error: '%s' is not a directory\n", token);
            free(path_copy);
            return -1;
        }

        struct inode next_inode;
        int found = traverse_directory(file, &current_inode, token,
                                       &next_inode, sb, partition_offset);
        if (!found) {
            fprintf(stderr, "Error: Path component '%s' not found\n", token);
            free(path_copy);
            return -1;
        }

        current_inode = next_inode;
        token = strtok_r(NULL, "/", &saveptr);
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

                printf("\nSubpartition table:");
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
            printf("%s:\n", path);
        }
        else if (path != NULL)
        {
            printf("/:\n");
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
        printf("/:\n");
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