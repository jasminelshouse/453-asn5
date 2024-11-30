CC = gcc
CFLAGS = -Wall -g
ARCH_FLAGS = -arch x86_64
 # Add this line for Apple Silicon Macs
SRC_DIR = src
BIN_DIR = bin
TARGETS = minls

all: $(TARGETS)

minls: $(SRC_DIR)/minls.c
	$(CC) $(CFLAGS) $(ARCH_FLAGS) -o $@ $^


clean:
	rm -f $(TARGETS)
