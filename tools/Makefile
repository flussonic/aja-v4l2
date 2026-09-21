CFLAGS ?= -O2 -Wall -Wextra -I../include
all: ajav
ajav: ajav.c ../include/ajav.h
	$(CC) $(CFLAGS) -o $@ ajav.c
clean:
	rm -f ajav
.PHONY: all clean
