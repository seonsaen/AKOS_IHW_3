CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11

all: master player observer

master: master.c common.h
	$(CC) $(CFLAGS) master.c -o master

player: player.c common.h
	$(CC) $(CFLAGS) player.c -o player

observer: observer.c common.h
	$(CC) $(CFLAGS) observer.c -o observer

clean:
	rm -f master player observer