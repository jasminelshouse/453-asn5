#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "minget.h"

#define S_ISDIR(mode) (((mode) & DIRECTORY) == DIRECTORY)

void print_usage() {
  printf("Usage: minget [-v] [-p part [-s sub]] imagefile srcpath [dstpath]\n");
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
    printf("  log_zone_size %d (zone size: %d bytes)\n", sb->log_zone_size, 
        zone_size);
    printf("  max_file %u\n", sb->max_file);
    printf("  magic 0x%x\n", sb->magic);
    printf("  zones %u\n", sb->zones);
    printf("  blocksize %u\n", sb->blocksize);
    printf("  subversion %u\n", sb->subversion);
}

/* read an inode by its number */
void read_inode(FILE *file, int inode_num, struct inode *inode,
 struct superblock *sb) {
    int inode_block = ((inode_num - 1) / (sb->blocksize / INODE_SIZE)) + 2;  
    int inode_index = (inode_num - 1) % (sb->blocksize / INODE_SIZE);
    fseek(file, sb->blocksize * inode_block + inode_index * INODE_SIZE, 
    SEEK_SET);
    fread(inode, sizeof(struct inode), 1, file);

    
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

FILE *open_destination(const char *dstpath) {
    if (dstpath == NULL) {
        return stdout; /*stdout if no destination path*/
    }
    FILE *dst = fopen(dstpath, "wb");
    if (!dst) {
        perror("Failed to open destination file");
        exit(EXIT_FAILURE);
    }
    return dst;
}

void copy_file_data(FILE *src, struct inode *inode, struct superblock *sb,
     FILE *dst) {
    char buffer[1024]; 
    int bytes_to_read = inode->size;
    int bytes_read;

    for (int i = 0; i < DIRECT_ZONES && bytes_to_read > 0; i++) {
        if (inode->zone[i] == 0) continue; 
        int block_address = sb->firstdata + (inode->zone[i] - 1);
        fseek(src, block_address * sb->blocksize, SEEK_SET);

        int chunk_size = 
            (bytes_to_read > sb->blocksize) ? sb->blocksize : bytes_to_read;
        bytes_read = fread(buffer, 1, chunk_size, src);
        fwrite(buffer, 1, bytes_read, dst);

        bytes_to_read -= bytes_read;
    }
}

int main(int argc, char *argv[]) {
    int verbose = 0;
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *srcpath = NULL;
    char *dstpath = NULL;
    FILE *file, *dst_file;
    struct superblock sb;
    struct inode inode;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            partition = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            subpartition = atoi(argv[++i]);
        } else if (!imagefile) {
            imagefile = argv[i];
        } else if (!srcpath) {
            srcpath = argv[i];
        } else if (!dstpath) {
            dstpath = argv[i];
        }
    }

    if (!imagefile || !srcpath) {
        print_usage();
        return EXIT_FAILURE;
    }

    file = fopen(imagefile, "rb");
    if (!file) {
        fprintf(stderr, "Error opening image file.\n");
        return EXIT_FAILURE;
    }

    int partition_offset = 0;
    if (partition != -1) {
        read_partition_table(file, partition, subpartition, &partition_offset);
        /* adjust fp to start of selected partition*/
        fseek(file, partition_offset, SEEK_SET);  
    }

    read_superblock(file, &sb);
    if (sb.magic != MAGIC_NUM) {
        fprintf(stderr, "Not a Minix filesystem.\n");
        fclose(file);
        return EXIT_FAILURE;
    }

    if (verbose) {
        print_superblock(&sb);
    }

    if (find_inode_by_path(file, srcpath, &inode, &sb) != 0) {
        fprintf(stderr, "File not found.\n");
        fclose(file);
        return EXIT_FAILURE;
    }

    if ((inode.mode & FILE_TYPE) != REGULAR_FILE) {
        fprintf(stderr, "Not a regular file.\n");
        fclose(file);
        return EXIT_FAILURE;
    }

    dst_file = open_destination(dstpath);
    copy_file_data(file, &inode, &sb, dst_file);

    if (dst_file != stdout) {
        fclose(dst_file);
    }
    fclose(file);
    return EXIT_SUCCESS;
}
