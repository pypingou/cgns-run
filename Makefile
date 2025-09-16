CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
TARGET = cgns-run
SOURCE = cgns-run.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/