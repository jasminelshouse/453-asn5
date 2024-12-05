#ifndef MINLS_H
#define MINLS_H

#include "shared.h"

/* Other declarations specific to minls */
#define INODE_SIZE 64
#define DIRECTORY_ENTRY_SIZE 64

/* Function declarations for minls */
void print_usage_minls(void);
int minls_main(int argc, char *argv[]);

#endif /* MINLS_H */
