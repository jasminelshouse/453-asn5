#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shared.h"

/* function declarations */
void copy_file_contents(FILE *file, struct inode *inode, struct superblock *sb,
int partition_offset, FILE *output);


void copy_file_contents(FILE *file, struct inode *inode, struct superblock *sb,
 int partition_offset, FILE *output) {
    /* simple implementation assuming all data is in direct zones */
    char *buffer = malloc(sb->blocksize);
    int i;
    for (i = 0; i < DIRECT_ZONES; i++) {
        if (inode->zone[i] == 0) continue;  /* skip empty zones */

        int block_address = partition_offset + 
        inode->zone[i] * sb->blocksize;
        fseek(file, block_address, SEEK_SET);
        int to_read = 
        (inode->size < sb->blocksize) ? inode->size : sb->blocksize;
        fread(buffer, to_read, 1, file);
        fwrite(buffer, to_read, 1, output);

        inode->size -= to_read;
        if (inode->size <= 0) break;  /* stop if no more data */
    }
}


int minget_main(int argc, char *argv[]) {
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

    /* argument parsing logic */
    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-v") == 0) {
                verbose = 1;
            } else if (strcmp(argv[i], "-p") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "Error: Missing value for -p\n");
                    print_usage_minget();
                    return EXIT_FAILURE;
                }
                partition = atoi(argv[i]);
            } else if (strcmp(argv[i], "-s") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "Error: Missing value for -s\n");
                    print_usage_minget();
                    return EXIT_FAILURE;
                }
                subpartition = atoi(argv[i]);
            } else {
                fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                print_usage_minget();
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
            print_usage_minget();
            return EXIT_FAILURE;
        }
    }

    if (!imagefile || !srcpath) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage_minget();
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
    int partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
    }

    /* read superblock of selected partition */
    read_superblock(file, &sb, partition_offset, verbose);

    /* find target inode based on the provided path */
    if (find_inode_by_path(file, srcpath, &src_inode, &sb) != 0) {
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
