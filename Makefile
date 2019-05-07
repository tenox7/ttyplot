CFLAGS += -Wall
LDLIBS += -lcurses

all: ttyplot

clean:
	rm -f ttyplot torture

.PHONY: all clean
