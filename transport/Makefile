CC = gcc
CFLAGS = -Wall -Wextra -std=c17

SOURCES = transport.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLE = transport

.PHONY: clean distclean

make: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	rm -f $(OBJECTS)

distclean: clean
	rm -f $(EXECUTABLE)
