
CFLAGS = -Wall -O3

all: ttyplot

ttyplot: ttyplot.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) -lcurses

clean:
	rm -f ttyplot
