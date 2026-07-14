VERSION    = 1.7.5
DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man
CPPFLAGS += -DVERSION_STR='"$(VERSION)"'
CFLAGS += -Wall -Wextra
CFLAGS += `pkg-config --cflags ncursesw`
LDLIBS += `pkg-config --libs ncursesw` -lm

# Experimental, opt-in ASCII-art rendering backend (aalib). Off by default;
# build with `make AA=1` (requires aalib and its aalib-config helper).
# Written without GNU-only `ifdef` so this file also parses under BSD make (bmake).
AALIB_CPPFLAGS_1 = -DAALIB `aalib-config --cflags`
AALIB_LDLIBS_1   = `aalib-config --libs`
CPPFLAGS += $(AALIB_CPPFLAGS_$(AA))
LDLIBS   += $(AALIB_LDLIBS_$(AA))

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
