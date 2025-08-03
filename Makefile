VERSION    = 1.7.3
DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man
CPPFLAGS += -DVERSION_STR='"$(VERSION)"'
CFLAGS += -Wall -Wextra
CFLAGS += `pkg-config --cflags ncursesw`
LDLIBS += `pkg-config --libs ncursesw` -lm

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
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

.PHONY: all clean install uninstall
