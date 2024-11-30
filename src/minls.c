#include <stdio.h>
#include <stdlib.h>

void print_usage() {
    printf("Usage: minls [-v][-p part[-s sub]] imagefile [path]\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /*Here we have to parse all the args, open the image file, read filesystem metadata*/
    return 0;
}
