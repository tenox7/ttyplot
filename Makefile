
all: ttyplot

ttyplot: ttyplot.c
	gcc ttyplot.c -o ttyplot -Wall -O3 -lcurses

clean:
	rm -f ttyplot
