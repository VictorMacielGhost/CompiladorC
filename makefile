CC = gcc
PYTHON ?= python

CFLAGS = -std=c99 -Wall -Wextra -g3 -Isrc

TARGET = builds/domusc.exe

SOURCES = $(wildcard src/*.c) \
          $(wildcard src/*/*.c)

OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(wildcard src/*.h) $(wildcard src/*/*.h)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)


%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	del /Q $(OBJECTS) $(TARGET)

test:
	$(PYTHON) tests/test_compiler.py

.PHONY: clean test