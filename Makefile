CFLAGS += -Wall -O3
LDLIBS += -lcurses

all: ttyplot

clean:
	rm -f ttyplot torture
