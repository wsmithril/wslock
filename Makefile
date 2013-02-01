CC = gcc
CFLAGS  = $(shell pkg-config --cflags xcb xcb-dpms xcb-keysyms cairo) -O2 -Wall -std=c99 -g
LDFLAGS = $(shell pkg-config --libs   xcb xcb-dpms xcb-keysyms cairo) -lcrypt -lm

OBJECTS = wslock.o timer.o lock_screen.o

UID := $(shell id -u)

.PHONY: all clean wslock

all: show-cfg wslock

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

wslock.c: timer.h lock_screen.h

timer.c: timer.h

lock_screen.c: lock_screen.h

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
