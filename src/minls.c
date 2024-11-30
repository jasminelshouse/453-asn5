#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minls.h"


void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
}

int main(int argc, char *argv[]) {
    int verbose = 0; 
    int partition = -1;
    int subpartition = -1;
    char *imagefile = NULL;
    char *path = NULL;

    /* if less than 2 arguments, show usage */
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /*Here we have to parse all the args, open the image file, read filesystem metadata*/

    /* argument parsing */

    for (int i = 1; i < argc; i++) {
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
    /* verbose output */
    if (verbose) {
        fprintf(stderr, "verbose mode enabled\n");
        fprintf(stderr, "partition: %d\n", partition);
        if (subpartition != -1) {
            fprintf(stderr, "subpartition: %d\n", subpartition);
        }
    }

    /* image file and path */
    printf("image file: %s\n", imagefile);
    if (path) {
        printf("path: %s\n", path);
    } else {
        printf("path: (root directory)\n");
    }

    /* close file */
    fclose(file); 
    return 0;
}
