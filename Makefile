DESTDIR ?=
PREFIX  ?= /usr/local

CFLAGS = -Wall -O3

all: ttyplot

ttyplot: ttyplot.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) -lcurses

torture: torture.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS)

install: ttyplot
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 ttyplot $(DESTDIR)$(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)$(PREFIX)/bin/ttyplot

clean:
	rm -f ttyplot torture

.PHONY: all clean install uninstall
