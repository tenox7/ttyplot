DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man
CFLAGS += -Wall -Wextra
LDLIBS += `pkg-config --libs ncurses 2>/dev/null || echo '-lcurses -ltinfo'`
torture: LDLIBS = -lm

all: ttyplot

install: ttyplot ttyplot.1
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(MANPREFIX)/man1
	install -m755 ttyplot   $(DESTDIR)$(PREFIX)/bin
	install -m644 ttyplot.1 $(DESTDIR)$(MANPREFIX)/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ttyplot
	rm -f $(DESTDIR)$(MANPREFIX)/man1/ttyplot.1

clean:
	rm -f ttyplot torture

.PHONY: all clean install uninstall
