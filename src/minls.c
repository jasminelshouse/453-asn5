#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "shared.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

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
    int i;
    int block_address;
    char *buffer = malloc(sb->blocksize);
    int offset = 0;
    if (!(dir_inode->mode & DIRECTORY)) {
        fprintf(stderr, "Error: Not a directory.\n");
        return;
    }

    if (!buffer) {
        fprintf(stderr, "Memory allocation failed.\n");
        return;
    }

    printf("/:\n");

    /* iterate through direct zones of directory inode */
    for (i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) continue;

        block_address = sb->firstdata;

        /* seek to calculated block position in file*/
        fseek(file, block_address * sb->blocksize, SEEK_SET);
        fread(buffer, sb->blocksize, 1, file);

        /* process each directory entry within block */
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


void print_partition_table(FILE *file, int partition_offset, 
    int print_subpartitions) {

    int i;
    uint8_t buffer[SECTOR_SIZE];
    struct partition_table *partitions = 
        (struct partition_table *)(buffer + PARTITION_TABLE_OFFSET);

    fseek(file, partition_offset, SEEK_SET);

    

    fread(buffer, SECTOR_SIZE, 1, file);

    printf("Partition table:\n");
    printf("       ----Start----      ------End-----\n");
    printf(
    "  Boot head  sec  cyl Type head  sec  cyl      First       Size\n");
    
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


int minls_main(int argc, char *argv[]) {
    int verbose = 0;
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *path = NULL;
    FILE *file;
    struct superblock sb;
    struct inode target_inode;
    int i;
    int partition_offset = 0;

   
    if (argc < 2) {
        print_usage_minls();
        return EXIT_FAILURE;
    }
    /** parse args */
    
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-v") == 0) {
                verbose = 1; 
            } else if (strcmp(argv[i], "-p") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "Error: Missing value for -p\n");
                    print_usage_minls();
                    return EXIT_FAILURE;
                }
                partition = atoi(argv[i]);
            } else if (strcmp(argv[i], "-s") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "Error: Missing value for -s\n");
                    print_usage_minls();
                    return EXIT_FAILURE;
                }
                subpartition = atoi(argv[i]);
            } else {
                fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                print_usage_minls();
                return EXIT_FAILURE;
            }
        } else if (!imagefile) {
            imagefile = argv[i];
        } else if (!path) {
            path = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage_minls();
            return EXIT_FAILURE;
        }
    }

    if (!imagefile) {
        fprintf(stderr, "Error: Missing image file\n");
        print_usage_minls();
        return EXIT_FAILURE;
    }

  
    file = fopen(imagefile, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open image file '%s'\n", imagefile);
        return EXIT_FAILURE;
    }

    /** read partition and subpart info **/
   
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


    if (sb.magic != MAGIC_NUM && sb.magic != R_MAGIC_NUM) {

        /* error message if not valid */
        fprintf(stderr, "Bad magic number. (0x%x)\nThis doesn’t look like "
               "a MINIX filesystem.\n", sb.magic);
        exit(EXIT_FAILURE);
    }

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

    list_directory(file, &target_inode, &sb);

    fclose(file);
    return EXIT_SUCCESS;
}
