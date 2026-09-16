CC = gcc

CFLAGS = -std=c99 -Wall -Wextra -g3 -Isrc

TARGET = builds/tinyc.exe

SOURCES = $(wildcard src/*.c) \
          $(wildcard src/*/*.c)

OBJECTS = $(SOURCES:.c=.o)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	del /Q $(OBJECTS) $(TARGET)