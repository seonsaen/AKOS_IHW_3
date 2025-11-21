CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2 -pthread

TARGET = rps
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) 4

clean:
	rm -f $(TARGET)