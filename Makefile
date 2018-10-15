NAME = ttyplot
CC = cc

.PHONY: all default clean

all: default
default: $(NAME)

$(NAME): ttyplot.c
	$(CC) -Wall -lncurses -o $(NAME) ttyplot.c

clean:
	rm $(NAME)
