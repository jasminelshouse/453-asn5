# Variables
CC = gcc
CFLAGS = -Wall -g -std=c99 -Wimplicit-function-declaration -D_GNU_SOURCE

# Targets
all: minls minget

minls: minls.c
	$(CC) $(CFLAGS) -o minls minls.c

minget: minget.c
	$(CC) $(CFLAGS) -o minget minget.c

clean:
	rm -f minls minget

# Phony targets
.PHONY: all clean
