#include "shared.h"

/* Shared implementations */
void print_usage_minget() {
printf("Usage: minget [-v] [-p part [-s sub]] imagefile srcpath [dstpath]\n");
printf("Options:\n"
    "\t-p\t part    --- select partition for filesystem (default: none)\n"
    "\t-s\t sub     --- select subpartition for filesystem (default: none)\n"
    "\t-h\t help    --- print usage information and exit\n"
    "\t-v\t verbose --- increase verbosity level\n");
}

void print_usage_minls() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
    printf("Options:\n"
    "\t-p\t part    --- select partition for filesystem (default: none)\n"
    "\t-s\t sub     --- select subpartition for filesystem (default: none)\n"
    "\t-h\t help    --- print usage information and exit\n"
    "\t-v\t verbose --- increase verbosity level\n");
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