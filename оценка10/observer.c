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
#include <dirent.h>
#include "common.h"

static char fifo_name[256];
static int fd_fifo = -1;

void cleanup(int sig) {
    (void)sig;
    if (fd_fifo != -1) {
        close(fd_fifo);
        fd_fifo = -1;
    }
    unlink(fifo_name);
    printf("\n[OBSERVER %d] Завершение работы, FIFO удален: %s\n", getpid(), fifo_name);
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    // Создаем уникальное FIFO для этого наблюдателя
    snprintf(fifo_name, sizeof(fifo_name), "%s/observer_%d.fifo", OBSERVERS_DIR, getpid());
    
    // Создаем каталог если не существует
    mkdir(OBSERVERS_DIR, 0777);
    
    // Удаляем старый FIFO если есть и создаем новый
    unlink(fifo_name);
    if (mkfifo(fifo_name, 0666) == -1) {
        perror("mkfifo");
        return 1;
    }

    printf("[OBSERVER %d] Создан FIFO: %s\n", getpid(), fifo_name);
    printf("[OBSERVER %d] Ожидание данных...\n", getpid());

    char buffer[256];
    int tournament_over = 0;

    while (!tournament_over) {
        fd_fifo = open(fifo_name, O_RDONLY);
        if (fd_fifo == -1) {
            perror("open");
            break;
        }

        ssize_t bytes;
        while ((bytes = read(fd_fifo, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes] = '\0';
            printf("[OBSERVER %d] %s", getpid(), buffer);
            fflush(stdout);

            // Проверяем сообщение о завершении турнира
            if (strstr(buffer, "Турнир завершен") != NULL) {
                tournament_over = 1;
                break;
            }
        }

        close(fd_fifo);
        fd_fifo = -1;
    }

    cleanup(0);
    return 0;
}