#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "common.h"

const char *STR_MOVES[] = {"Камень", "Ножницы", "Бумага"};

static SharedData *shared = NULL;
static sem_t *sem_output = NULL;
static sem_t *sem_turn_complete = NULL;
static sem_t *sem_start = NULL;
static int id = -1;
static int fd_shm = -1;

static void cleanup_player(void) {
    fprintf(stderr, "[PLAYER %d] Очистка ресурсов...\n", id);

    if (sem_output) {
        sem_close(sem_output);
        sem_output = NULL;
    }
    if (sem_turn_complete) {
        sem_close(sem_turn_complete);
        sem_turn_complete = NULL;
    }
    if (sem_start) {
        sem_close(sem_start);
        sem_start = NULL;
    }
    if (shared && shared != MAP_FAILED) {
        munmap(shared, sizeof(SharedData));
        shared = NULL;
    }
    if (fd_shm != -1) {
        close(fd_shm);
        fd_shm = -1;
    }
    fprintf(stderr, "[PLAYER %d] Очистка закончена\n", id);
}

static void sig_handler(int sig) {
    (void)sig;
    fprintf(stderr, "\n[PLAYER %d] Получен сигнал - завершаемся\n", id);
    cleanup_player();
    exit(0);
}

// Функция для отправки форматированной строки Наблюдателю
void send_to_observer(const char *fmt, ...) {
    int fd = open(OBSERVER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        // Если наблюдатель не запущен, просто игнорируем
        return;
    }

    char buffer[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len > 0) {
        if (write(fd, buffer, len) == -1) {
            // Ошибка записи (игнорируем)
        }
    }
    close(fd);
}

static int connect_to_resources(void) {
    // Открываем shm
    fd_shm = shm_open(SHM_NAME, O_RDWR, 0);
    if (fd_shm == -1) {
        perror("shm_open");
        return -1;
    }

    shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        close(fd_shm);
        return -1;
    }

    // Открываем семафоры
    sem_output = sem_open(SEM_OUTPUT_MUTEX, 0);
    if (sem_output == SEM_FAILED) {
        perror("sem_open output");
        return -1;
    }

    sem_turn_complete = sem_open(SEM_TURN_COMPLETE, 0);
    if (sem_turn_complete == SEM_FAILED) {
        perror("sem_open turn_complete");
        return -1;
    }

    char sname[64];
    build_start_name(id, sname, sizeof(sname));
    sem_start = sem_open(sname, 0);
    if (sem_start == SEM_FAILED) {
        perror("sem_open sem_start");
        return -1;
    }

    return 0;
}

static void register_player(void) {
    sem_wait(sem_output);
    shared->registered_players++;
    printf("[PLAYER %d] Зарегистрирован (PID=%d). Всего: %d/%d\n",
           id, getpid(), shared->registered_players, shared->total_players);
    sem_post(sem_output);
    send_to_observer("[OBSERVER] Игрок %d (PID %d) подключился.\n", id, getpid());
}

static void player_loop(void) {
    srand(time(NULL) ^ (getpid() << 16) ^ id);

    sem_wait(sem_output);
    printf("[PLAYER %d] Готов к турниру\n", id);
    sem_post(sem_output);

    // Основной цикл
    while (1) {
        if (sem_wait(sem_start) == -1) {
            if (errno == EINTR) continue;
            perror("sem_wait start");
            break;
        }

        if (shared->game_over) {
            sem_wait(sem_output);
            printf("[PLAYER %d] Получен сигнал завершения турнира\n", id);
            printf("[PLAYER %d] Итоговый счет %d очков\n", id, shared->scores[id]);
            sem_post(sem_output);
            break;
        }

        int mv = rand() % 3;
        shared->current_moves[id] = mv;

        sem_wait(sem_output);
        printf("[PLAYER %d] Участвует в матче %d, ход: %s\n", id, shared->matches_played, STR_MOVES[mv]);
        sem_post(sem_output);
        send_to_observer("[OBSERVER] Игрок %d выбрал %s\n", id, STR_MOVES[mv]);

        // Сообщаем мастеру о завершении хода
        sem_post(sem_turn_complete);
    }

    sem_wait(sem_output);
    printf("[PLAYER %d] Завершает участие в турнире\n", id);
    sem_post(sem_output);
    send_to_observer("[OBSERVER] Игрок %d завершил участие\n", id);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <ID>\n", argv[0]);
        return 1;
    }

    id = atoi(argv[1]);
    if (id < 0 || id >= MAX_PLAYERS) {
        fprintf(stderr, "ID должен быть 0..%d\n", MAX_PLAYERS-1);
        return 1;
    }

    // Установка обработчиков сигналов
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    printf("=== Процесс игрока %d (PID: %d) ===\n", id, getpid());

    // Подключение к ресурсам
    if (connect_to_resources() == -1) {
        cleanup_player();
        return 1;
    }

    // Регистрация
    register_player();

    // Основная логика
    player_loop();

    // Очистка
    cleanup_player();

    printf("[PLAYER %d] Программа завершена\n", id);
    return 0;
}