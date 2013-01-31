CC = gcc
CFLAGS  = $(shell pkg-config --cflags xcb xcb-dpms xcb-keysyms) -O2 -Wall -std=c99 -g
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
	@ echo "[ROOT]: chown root:root" $@ && chown root:root $@
	@ echo "[ROOT]: chmod u+s" $@ && chmod u+s $@

show-cfg:
	@ echo "Complie configuration:"
	@ echo "CC      =" $(CC)
	@ echo "CFLAGS  =" $(CFLAGS)
	@ echo "LDFLAGS =" $(LDFLAGS)

clean:
	rm -f wslock $(OBJECTS)
