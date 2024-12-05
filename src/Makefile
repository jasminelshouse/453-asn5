CC = gcc
CFLAGS = -std=c99 -Wall
SRC = main.c minget.c minls.c shared.c
TARGET = all

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
