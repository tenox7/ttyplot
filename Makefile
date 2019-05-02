
CFLAGS = -Wall -O3

all: ttyplot

ttyplot: ttyplot.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) -lcurses

torture: torture.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f ttyplot torture
