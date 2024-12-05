#ifndef MINGET_H
#define MINGET_H

#include "shared.h"

/* Other declarations specific to minget */
#define INODE_SIZE 64
#define DIRECTORY_ENTRY_SIZE 64

/* Function declarations for minget */
void print_usage_minget();
int minget_main(int argc, char *argv[]);

#endif /* MINGET_H */
