CC = gcc
CFLAGS = -Wall -Wextra -std=c11

all: master player

master: master.c common.h
	$(CC) $(CFLAGS) master.c -o master

player: player.c common.h
	$(CC) $(CFLAGS) player.c -o player

clean:
	rm -f master player