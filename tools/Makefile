# SPDX-License-Identifier: (GPL-2.0 OR MIT)
# Copyright (C) 2026 Max Lapshin <max@flussonic.com>
CFLAGS ?= -O2 -Wall -Wextra -I../include
all: ajav
ajav: ajav.c ../include/ajav.h
	$(CC) $(CFLAGS) -o $@ ajav.c -lm
clean:
	rm -f ajav
.PHONY: all clean
