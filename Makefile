DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man

CFLAGS = -Wall -O3

all: ttyplot

ttyplot: ttyplot.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) -lcurses

torture: torture.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS)

install: ttyplot ttyplot.1
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(MANPREFIX)/man1
	install -m755 ttyplot   $(DESTDIR)$(PREFIX)/bin
	install -m644 ttyplot.1 $(DESTDIR)$(MANPREFIX)/man1

uninstall:
	rm -f $(PREFIX)$(PREFIX)/bin/ttyplot
	rm -f $(PREFIX)$(MANPREFIX)/man1/ttyplot.1

clean:
	rm -f ttyplot torture

.PHONY: all clean install uninstall
