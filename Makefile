DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man

# Optional, thus no override.

CFLAGS += -Wall -Wextra

# The following variables are overridden because the ncursesw flags are
# required for a successful build.

override CFLAGS += $(shell pkg-config --cflags ncursesw)
override LDLIBS += $(shell pkg-config --libs ncursesw) -lm

all: ttyplot stresstest

install: ttyplot ttyplot.1
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(MANPREFIX)/man1
	install -m755 ttyplot   $(DESTDIR)$(PREFIX)/bin
	install -m644 ttyplot.1 $(DESTDIR)$(MANPREFIX)/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ttyplot
	rm -f $(DESTDIR)$(MANPREFIX)/man1/ttyplot.1

clean:
	rm -f ttyplot stresstest

.c:
	@pkg-config --version > /dev/null
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

.PHONY: all clean install uninstall
