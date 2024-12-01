#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minls.h"


void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
}

void print_permissions(uint16_t mode) {
    printf("%c", (mode & DIRECTORY) ? 'd' : '-');
    printf("%c%c%c", (mode & OWR_PERMISSION) ? 'r' : '-',
                     (mode & OWW_PERMISSION) ? 'w' : '-',
                     (mode & OWE_PERMISSION) ? 'x' : '-');
    printf("%c%c%c", (mode & GR_PERMISSION) ? 'r' : '-',
                     (mode & GW_PERMISSION) ? 'w' : '-',
                     (mode & GE_PERMISSION) ? 'x' : '-');
    printf("%c%c%c ", (mode & OTR_PERMISSION) ? 'r' : '-',
                      (mode & OTW_PERMISSION) ? 'w' : '-',
                      (mode & OTE_PERMISSION) ? 'x' : '-');
}

/* read the superblock */
void read_superblock(FILE *file, struct superblock *sb) {
    /* superblock starts at 1024 */
    fseek(file, 1024, SEEK_SET);
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
    printf("  log_zone_size %d (zone size: %d bytes)\n", sb->log_zone_size, zone_size);
    printf("  max_file %u\n", sb->max_file);
    printf("  magic 0x%x\n", sb->magic);
    printf("  zones %u\n", sb->zones);
    printf("  blocksize %u\n", sb->blocksize);
    printf("  subversion %u\n", sb->subversion);
}



/* read inode*/
void read_inode(FILE *file, struct superblock *sb, uint32_t inode_num, int partition_offset, struct inode *inode) {
    uint32_t inode_table_start = partition_offset + 1024 + sb->i_blocks * sb->blocksize + sb->z_blocks * sb->blocksize;
    printf("\nCalculated inode_table_start: %u\n", inode_table_start);

    uint32_t inode_offset = inode_table_start + (inode_num - 1) * INODE_SIZE;
    printf("Calculated inode_offset for inode %u: %u\n", inode_num, inode_offset);

    fseek(file, inode_offset, SEEK_SET);
    fread(inode, sizeof(struct inode), 1, file);

}



/* print inode */
#include <time.h>

void print_inode(struct inode *inode) {
   /* raw hex */
    printf("  uint16_t mode 0x%x (", inode->mode);
    print_permissions(inode->mode);
    printf(")\n");

    printf("  uint16_t links %u\n", inode->links);
    printf("  uint16_t uid %u\n", inode->uid);
    printf("  uint16_t gid %u\n", inode->gid);
    printf("  uint32_t size %u\n", inode->size);

    time_t atime = inode->atime;
    time_t mtime = inode->mtime;
    time_t c_time = inode->c_time;


    printf("  uint32_t atime %u --- %s", (uint32_t)atime, ctime(&atime));
    printf("  uint32_t mtime %u --- %s", (uint32_t)mtime, ctime(&mtime));
    printf("  uint32_t ctime %u --- %s", (uint32_t)c_time, ctime(&c_time));
}


void list_directory(FILE *file, struct superblock *sb, struct inode *dir_inode, int partition_offset) {
    char buffer[sb->blocksize]; 
    int entries_per_block = sb->blocksize / sizeof(struct fileent);

    printf("/:\n");
    int i; 
    int j; 
    for (i = 0; i < DIRECT_ZONES; i++) {
        if (dir_inode->zone[i] == 0) {
            printf("Zone %d is empty\n", i);
            continue;
        }

       /* calculate zone offset */
        uint32_t zone_start = partition_offset + dir_inode->zone[i] * sb->blocksize;
        fseek(file, zone_start, SEEK_SET);
        fread(buffer, sb->blocksize, 1, file);

        struct fileent *entries = (struct fileent *)buffer;

        /* iterate through directory entries */
        for (j = 0; j < entries_per_block; j++) {
            if (entries[j].ino == 0) {
                continue; 
            }

            
            struct inode entry_inode;
            read_inode(file, sb, entries[j].ino, partition_offset, &entry_inode);

            print_permissions(entry_inode.mode);
            printf("%9u ", entry_inode.size);
            printf("%s\n", entries[j].name);
        }
    }
}


void debug_inode_zones(struct inode *inode) {
    int i; 
    printf("Debugging inode zones:\n");
    for ( i = 0; i < DIRECT_ZONES; i++) {
        printf("zone[%d] = %u\n", i, inode->zone[i]);
    }
    printf("Indirect zone = %u\n", inode->indirect);
    printf("Double indirect zone = %u\n", inode->two_indirect);
}



void read_partition_table(FILE *file, int partition, int subpartition, int *partition_offset) {
    uint8_t buffer[512];

    /*move fp to the start of the disk and read*/
    fseek(file, 0, SEEK_SET);
    fread(buffer, 512, 1, file);

    /*the signature at the end of the first part is 0x55AA
    to be valid*/
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        fprintf(stderr, "error: invalid partition table signature\n");
        fclose(file);
        exit(1);
    }

    /* Read the primary partition table*/
    struct partition_table *partitions = (struct partition_table *)&buffer[446];

    /* Validate the primary partition index, should be less than 3*/
    if (partition < 0 || partition > 3) {
        fprintf(stderr, "error: invalid primary partition number\n");
        fclose(file);
        exit(1);
    }

    /* Calculate the offset for the selected partition*/
    *partition_offset = partitions[partition].IFirst * 512;

    /* Handle subpartition if provided, has not been tested*/
    struct partition_table *subpartitions = NULL;
    if (subpartition != -1) {
        /* Seek to the partition's first sector and read its subpartition table*/
        fseek(file, *partition_offset, SEEK_SET);
        fread(buffer, 512, 1, file);
        subpartitions = (struct partition_table *)&buffer[446];

        /* Validate the subpartition index*/
        if (subpartition < 0 || subpartition > 3) {
            fprintf(stderr, "error: invalid subpartition number\n");
            fclose(file);
            exit(1);
        }

        /* Ensure the subpartition is Minix*/
        if (subpartitions[subpartition].type != 0x81) {
            fprintf(stderr, "error: invalid Minix subpartition type\n");
            fclose(file);
            exit(1);
        }

        /* Adjust the partition offset for the subpartition*/
        *partition_offset += subpartitions[subpartition].IFirst * 512;

        /* Debugging output for subpartition*/
        printf("Subpartition %d: lFirst=%u, size=%u\n", 
               subpartition, 
               subpartitions[subpartition].IFirst, 
               subpartitions[subpartition].size);
    }

    /* Debugging output for primary partition*/
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

    /* if less than 2 arguments, show usage */
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /*Here we have to parse all the args, open the image file, read filesystem metadata*/

    /* argument parsing */
    
    for (i = 1; i < argc; i++) {
        /* see if arg is an option */
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-v") == 0) {
                /* enter verbose mode */
                verbose = 1;
            } else if (strcmp(argv[i], "-p") == 0) {
                /* check if partition num is given */
                if (i + 1 >= argc) {  
                    fprintf(stderr, "error: missing val for -p\n");
                    print_usage();
                    return 1;
                }
                /* parse partition num */
                partition = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-s") == 0) {
                /* check if subpartition num is given */
                if (i + 1 >= argc) {  
                    fprintf(stderr, "error: missing val for -s\n");
                    print_usage();
                    return 1;
                }
                /* parse subpartition num */
                subpartition = atoi(argv[++i]);
            } else {
                /* unknown stuff */
                fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
                print_usage();
                return 1;
            }
        } else if (imagefile == NULL) {
            /* set image file if not already set */
            imagefile = argv[i];
        } else if (path == NULL) {
            /* set path if not already set */
            path = argv[i];
        } else {
            /* too many arguments */
            fprintf(stderr, "error: too many arguments\n");
            print_usage();
            return 1;
        }
    }

    /* check if img file is given */
    if (imagefile == NULL) {
        fprintf(stderr, "error: missing img file\n");
        print_usage();
        return 1;
    }

    /* open image file */
    FILE *file = fopen(imagefile, "rb"); 
    if (file == NULL) {
        fprintf(stderr, "error: cannot open image file '%s'\n", imagefile);
        return 1;
    }

    /* Determine partition offset */
    int partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
    }
    printf("Calculated partition offset: %d bytes\n", partition_offset);


    /* Adjust file pointer to the start of the partition */
    fseek(file, partition_offset, SEEK_SET);

    /* read superblock */
    struct superblock sb;
    read_superblock(file, &sb);

    /* Validate superblock */
    if (sb.magic != 0x4D5A) {
        fprintf(stderr, "error: invalid Minix filesystem (magic number mismatch: 0x%x)\n", sb.magic);
        fclose(file);
        return 1;
    }

    struct inode root_inode;
    read_inode(file, &sb, 1, partition_offset, &root_inode);
    list_directory(file, &sb, &root_inode, partition_offset);

    debug_inode_zones(&root_inode);

    /* verbose output */
    if (verbose) {
        // fprintf(stderr, "partition: %d\n", partition);
        // if (subpartition != -1) {
        //     fprintf(stderr, "subpartition: %d\n", subpartition);
        // }
        print_superblock(&sb);
        
        printf("\nFile inode:\n");
        print_inode(&root_inode);

    }

    /* image file and path */
    printf("\nimage file: %s\n", imagefile);
    if (path) {
        printf("path: %s\n", path);
    } else {
        printf("path: (root directory)\n");
    }


    /* close file */
    fclose(file); 
    return 0;
}
