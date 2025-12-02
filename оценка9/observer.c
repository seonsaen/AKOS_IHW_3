#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "common.h"

// Глобальные переменные для очистки
static int fd_fifo = -1;

void cleanup(int sig) {
    (void)sig;
    if (fd_fifo != -1) {
        close(fd_fifo);
    }
    unlink(OBSERVER_FIFO);
    printf("\n[OBSERVER] Завершение работы, канал удален\n");
    exit(0);
}



int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    unlink(OBSERVER_FIFO);
    if (mkfifo(OBSERVER_FIFO, 0666) == -1) {
        perror("mkfifo");
        return 1;
    }

    printf("[OBSERVER] Ожидание данных в канале %s...\n", OBSERVER_FIFO);

    char buffer[256];
    int tournament_over = 0;

    while (!tournament_over) {
        fd_fifo = open(OBSERVER_FIFO, O_RDONLY);
        if (fd_fifo == -1) {
            perror("open");
            break;
        }

        ssize_t bytes;
        while ((bytes = read(fd_fifo, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes] = '\0';
            printf("%s", buffer);
            fflush(stdout);

            // Проверяем сообщение о завершении турнира
            if (strstr(buffer, "Турнир завершен") != NULL) {
                tournament_over = 1;
                break;
            }
        }

        close(fd_fifo);
    }

    cleanup(0);
    return 0;
}