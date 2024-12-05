#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "minls.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
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






void print_computed_fields(struct superblock *sb) {
    int zone_size = sb->blocksize * (1 << sb->log_zone_size);
    int ptrs_per_zone = zone_size / sizeof(uint32_t); 
    int ino_per_block = sb->blocksize / INODE_SIZE;  
    int fileent_size = DIRECTORY_ENTRY_SIZE;         
    int ent_per_zone = zone_size / fileent_size;      

    /* compute the version based on magic number*/
    int version = (sb->magic == MAGIC_NUM) ? 3 : 
        (sb->magic == MAGIC_NUM_OLD) ? 2 : 0;

    /* first inode map, zone map, and inode block*/
    /*typical start*/
    int firstImap = 2; 
    int firstZmap = firstImap + sb->i_blocks;
    int firstIblock = firstZmap + sb->z_blocks;

    /* wrongended (check for byte-swapped magic number)*/
    int wrongended = (sb->magic == R_MAGIC_NUM || sb->magic == 
        R_MAGIC_NUM_OLD) ? 1 : 0;

    
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
    printf("  max_filename      %d\n", DIRSIZ);
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

void list_directory(FILE *file, struct inode *dir_inode, 
    const struct superblock *sb, int partition_offset) {
    /* validate that the inode represents a directory. */
    if (!(dir_inode->mode & DIRECTORY)) {
        fprintf(stderr, "Error: Inode is not a directory.\n");
        return;
    }

    printf("/:\n");

    /* iterate through the direct zones in the inode. */
    int i;
    for ( i = 0; i < DIRECT_ZONES; i++) {
        /** skip unused or uninitialized zones **/
        if (dir_inode->zone[i] == 0) {
            fprintf(stderr, 
                "DEBUG: Zone %d is unused or uninitialized.\n", i);
            continue;
        }
     /** calculate block address for current zone **/
        int block_address;
        block_address = 
            partition_offset + dir_inode->zone[i] * sb->blocksize;
        fseek(file, block_address, SEEK_SET);

        
        char buffer[sb->blocksize];
        fread(buffer, sb->blocksize, 1, file);

        int offset = 0;
        /*parse entries in the dir block*/
        while (offset < sb->blocksize) {
            struct fileent *entry = (struct fileent *)(buffer + offset);
            /** skip invalid directory entry**/
            if (entry->ino == 0) {
                offset += DIRECTORY_ENTRY_SIZE;
                continue;
            }

            /** calculate inode location for the directory entry **/
            int inodes_per_block = sb->blocksize / INODE_SIZE;
            int inode_block = sb->firstdata + 
                (entry->ino / inodes_per_block);
            int inode_index = entry->ino % inodes_per_block;
            int inode_offset = partition_offset + 
                (inode_block * sb->blocksize) + (inode_index * INODE_SIZE);

            struct inode entry_inode;
            read_inode(file, inode_offset, &entry_inode,sb);

            printf("%s %5d %s\n",
                   get_permissions(entry_inode.mode),
                   entry_inode.size,
                   entry->name);

            offset += DIRECTORY_ENTRY_SIZE;
        }
    }
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



void print_partition_table(FILE *file, int partition_offset, 
    int print_subpartitions) {
    fseek(file, partition_offset, SEEK_SET);
    uint8_t buffer[SECTOR_SIZE];
    fread(buffer, SECTOR_SIZE, 1, file);

    struct partition_table *partitions = 
        (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);
    printf("Partition table:\n");
    printf("       ----Start----      ------End-----\n");
    printf(
    "  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");
    int i;
    for (i = 0; i < 4; i++) {
       
        int start_cyl = 
        (partitions[i].start_sec & 0xC0) << 2 | partitions[i].start_cyl;
        int end_cyl = 
        (partitions[i].end_sec & 0xC0) << 2 | partitions[i].end_cyl;
        printf(
        "  0x%02X    %2d    %2d  %4d 0x%02X    %2d   %2d  %4d %10u %10u\n",
               partitions[i].bootind,
               partitions[i].start_head,
               partitions[i].start_sec & 0x3F,  
               start_cyl,
               partitions[i].type,
               partitions[i].end_head,
               partitions[i].end_sec & 0x3F,  
               end_cyl,
               partitions[i].IFirst,
               partitions[i].size);
    }

    if (print_subpartitions) {
        int i;
        for (i = 0; i < 4; i++) {
            if (partitions[i].type != 0 && partitions[i].size > 1) {
               
                uint32_t subpartition_offset = 
                partitions[i].IFirst * SECTOR_SIZE + partition_offset;

                fseek(file, subpartition_offset, SEEK_SET);
                fread(buffer, SECTOR_SIZE, 1, file);

                struct partition_table *subpartitions = 
                (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);
                if((subpartitions[i].start_head == 0)&&(
                    subpartitions[i].end_head == 0)){
                    break;
                }
                printf("\nSubpartition table:\n");
                printf("       ----Start----      ------End-----\n");
                printf(
        "  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");
                int j = 0;
                for ( j = 0; j < 4; j++) {
                    
                    int start_cyl = 
                        (subpartitions[j].start_sec & 0xC0) << 2 |
                            subpartitions[j].start_cyl;
                    int end_cyl = 
                        (subpartitions[j].end_sec & 0xC0) << 2 | 
                            subpartitions[j].end_cyl;
                    printf(
        "  0x%02X    %2d    %2d  %4d 0x%02X    %2d   %2d  %4d %10u %10u\n",
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


int main(int argc, char *argv[]) {
    int verbose = 0;
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *path = NULL;
    FILE *file;
    struct superblock sb;
    struct inode target_inode;

   
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }
    /** parse args */
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

  
    file = fopen(imagefile, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open image file '%s'\n", imagefile);
        return EXIT_FAILURE;
    }

    /** read partition and subpart info **/
    int partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, 
            &partition_offset);

        if (verbose) {
            /* printf("DEBUG: Partition offset = %d bytes\n",
                 partition_offset);*/
            print_partition_table(file, 0, 1);
        }
    }

   /** read superblock of selected partition. **/
    read_superblock(file, &sb, partition_offset, verbose);
    if (verbose) {
        print_superblock(&sb);
        print_computed_fields(&sb);
    }

   /*find target i node based on the provide path*/
    if (!path) {
        read_inode(file, 1, &target_inode, &sb); 
    } else {
        if (find_inode_by_path(file, path, &target_inode, &sb) != 0) {
            fprintf(stderr, "Error: Path not found '%s'\n", path);
            fclose(file);
            return EXIT_FAILURE;
        }
    }

  
    if (verbose) {
        print_inode(&target_inode);
    }

    

    fclose(file);
    return EXIT_SUCCESS;
}
