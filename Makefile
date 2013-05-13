CC = clang
PKG_DEVEL = xcb xcb-dpms xcb-keysyms cairo

CFLAGS  = $(shell pkg-config --cflags $(PKG_DEVEL)) -O2 \
          -Wall -std=c99 -g -DUSE_PAM
LDFLAGS = $(shell pkg-config --libs $(PKG_DEVEL)) -lcrypt -lm -lpam

OBJECTS = wslock.o timer.o lock_screen.o

PREFIX = /usr/local

.PHONY: all clean setsuid

all: show-cfg wslock

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

wslock.c: timer.h lock_screen.h

timer.c: timer.h

lock_screen.c: lock_screen.h

wslock: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

install: wslock wslock-password
	install wslock $(PREFIX)/bin
	install wslock-password /etc/pam.d -m 644

show-cfg:
	@ echo "Complie configuration:"
	@ echo "CC      =" $(CC)
	@ echo "CFLAGS  =" $(CFLAGS)
	@ echo "LDFLAGS =" $(LDFLAGS)

clean:
	rm -f wslock $(OBJECTS)
