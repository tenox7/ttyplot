
all: ttyplot

ttyplot: ttyplot.c
	gcc ttyplot.c -o ttyplot -Wall -O3 -lncurses

clean:
	rm -f ttyplot
