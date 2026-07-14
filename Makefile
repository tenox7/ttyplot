VERSION    = 1.4.1
DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man

# Legacy build: narrow-character curses only. No pkg-config, no ncursesw, no -lm.
CC        ?= cc
CPPFLAGS  += -DVERSION_STR='"$(VERSION)"'
CURSES    ?= -lcurses
LDLIBS    += $(CURSES)

# Experimental, opt-in aalib ASCII-art backend (the -A / -f flags). Off by
# default; build with `make AA=1` (needs aalib and its aalib-config helper).
# Written without GNU-only `ifdef`, so this also parses under BSD make (bmake).
AALIB_CPPFLAGS_1 = -DAALIB `aalib-config --cflags`
AALIB_LDLIBS_1   = `aalib-config --libs`
CPPFLAGS += $(AALIB_CPPFLAGS_$(AA))
LDLIBS   += $(AALIB_LDLIBS_$(AA))

# Portability knobs for old systems -- add to CPPFLAGS as needed:
#   -DNOACS        terminal lacks ACS_* line-drawing chars (use ASCII - | L)
#   -DNOGETMAXYX   curses lacks getmaxyx() (fall back to LINES/COLS)
# Pick the curses library name your platform uses, e.g.:
#   make CURSES=-lncurses     (Linux, modern BSD)
#   make CURSES=-lcurses      (SVr4: UnixWare, Solaris, IRIX, Tru64, AIX...)
# Known-crusty examples:
#   UnixWare/QNX : make CPPFLAGS=-DNOACS CURSES=-lcurses
#   AIX          : make CURSES=-lcurses   (-D_AIX predefined by xlc)
#   Solaris      : make CURSES=-lcurses   (__sun predefined)

all: ttyplot

ttyplot: ttyplot.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) ttyplot.c $(LDLIBS) -o ttyplot

install: ttyplot ttyplot.1
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(MANPREFIX)/man1
	install -m755 ttyplot   $(DESTDIR)$(PREFIX)/bin
	install -m644 ttyplot.1 $(DESTDIR)$(MANPREFIX)/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ttyplot
	rm -f $(DESTDIR)$(MANPREFIX)/man1/ttyplot.1

clean:
	rm -f ttyplot

.PHONY: all install uninstall clean
