.DEFAULT_GOAL := ttyplot

ttyplot:
	cc ttyplot.c -lncurses -o ttyplot

.PHONY: clean
clean:
	- rm -rf ttyplot
