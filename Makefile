CC = gcc
CFLAGS  = $(shell pkg-config --cflags xcb xcb-dpms xcb-keysyms) -O2 -Wall -std=c99
LDFLAGS = $(shell pkg-config --libs   xcb xcb-dpms xcb-keysyms) -lcrypt -lm

OBJECTS = wslock.o timer.o

UID := $(shell id -u)

.PHONY: all clean wslock

all: show-cfg wslock

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

wslock.c: timer.h

timer.c: timer.h

wslock: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	@ [ $(UID) -eq 0 ] || ( echo Form here on, we needs root && exit 1 )
	chown root:root wslock
	chmod u+s wslock

show-cfg:
	@ echo "Complie configuration:"
	@ echo "CC      =" $(CC)
	@ echo "CFLAGS  =" $(CFLAGS)
	@ echo "LDFLAGS =" $(LDFLAGS)

clean:
	rm -f wslock $(OBJECTS)
