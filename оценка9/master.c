#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "common.h"

const char *STR_MOVES[] = {"Камень", "Ножницы", "Бумага"};

static SharedData *shared = NULL;
static int fd_shm = -1;
static sem_t *sem_output = NULL;
static sem_t *sem_turn_complete = NULL;
static sem_t *sem_start[MAX_PLAYERS];
static int N = 0;

static void unlink_if_exists(const char *name) {
    sem_unlink(name);
}

static void cleanup_master(void) {
    fprintf(stderr, "[MASTER] Начинаем очистку ресурсов...\n");

    // Установим флаг game_over
    if (shared) {
        shared->game_over = true;
    }

    // Закрыть и удалить семафоры
    if (sem_output) {
        sem_close(sem_output);
        sem_unlink(SEM_OUTPUT_MUTEX);
        sem_output = NULL;
    }
    if (sem_turn_complete) {
        sem_close(sem_turn_complete);
        sem_unlink(SEM_TURN_COMPLETE);
        sem_turn_complete = NULL;
    }

    for (int i = 0; i < N; ++i) {
        if (sem_start[i]) {
            char name[64];
            build_start_name(i, name, sizeof(name));
            sem_close(sem_start[i]);
            sem_unlink(name);
            sem_start[i] = NULL;
        }
    }

    if (shared && shared != MAP_FAILED) {
        munmap(shared, sizeof(SharedData));
        shared = NULL;
    }
    if (fd_shm != -1) {
        close(fd_shm);
        shm_unlink(SHM_NAME);
        fd_shm = -1;
    }
    fprintf(stderr, "[MASTER] Ресурсы очищены\n");
}

static void sigint_handler(int sig) {
    (void)sig;
    fprintf(stderr, "\n[MASTER] Получен сигнал - начинаем корректное завершение\n");
    cleanup_master();
    exit(0);
}

static int judge(int m1, int m2) {
    if (m1 == m2) return 0;
    if ((m1 == ROCK && m2 == SCISSORS) ||
        (m1 == SCISSORS && m2 == PAPER) ||
        (m1 == PAPER && m2 == ROCK)) return 1;
    return 2;
}

// Функция для отправки форматированной строки Наблюдателю
void send_to_observer(const char *fmt, ...) {
    int fd = open(OBSERVER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        // Если наблюдатель не запущен, просто игнорируем (не блокируем игру)
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

static void wait_for_registration(void) {
    fprintf(stdout, "[MASTER] Ожидание регистрации %d игроков...\n", N);

    int last_reported = -1;
    while (shared->registered_players < N) {
        if (shared->registered_players != last_reported) {
            sem_wait(sem_output);
            fprintf(stdout, "[MASTER] Зарегистрировано: %d / %d\n",
                   shared->registered_players, N);
            sem_post(sem_output);
            last_reported = shared->registered_players;
        }
        sleep(1);

        // Проверка на game_over на случай прерывания
        if (shared->game_over) {
            fprintf(stderr, "[MASTER] Турнир прерван во время ожидания регистрации\n");
            return;
        }
    }

    sem_wait(sem_output);
    fprintf(stdout, "[MASTER] Все игроки зарегистрировались. Начинаем турнир\n");
    sem_post(sem_output);

    send_to_observer("[OBSERVER] Все игроки зарегистрировались. Начало турнира\n");
}

static void run_tournament(void) {
    shared->matches_played = 0;
    shared->total_matches = N * (N - 1) / 2;

    printf("\n=== Начало турнира! Всего матчей: %d ===\n", shared->total_matches);

    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            // Проверка на прерывание
            if (shared->game_over) {
                fprintf(stderr, "[MASTER] Турнир прерван\n");

                return;
            }

            shared->matches_played++;

            sem_wait(sem_output);
            printf("\n--- Матч %d/%d: Игрок %d vs Игрок %d ---\n",
                   shared->matches_played, shared->total_matches, i, j);
            sem_post(sem_output);

            send_to_observer("--- Матч %d/%d: Игрок %d vs Игрок %d ---\n",
                            shared->matches_played, shared->total_matches, i, j);

            // Пробуждаем игроков i и j
            sem_post(sem_start[i]);
            sem_post(sem_start[j]);

            // Ждём два сигнала о завершении хода
            sem_wait(sem_turn_complete);
            sem_wait(sem_turn_complete);

            int m1 = shared->current_moves[i];
            int m2 = shared->current_moves[j];
            int res = judge(m1, m2);

            sem_wait(sem_output);
            printf("  Ходы: %s vs %s\n", STR_MOVES[m1], STR_MOVES[m2]);
            if (res == 0) {
                printf("  Ничья (+1,+1)\n");
                send_to_observer("  Ничья! Оба игрока получают по 1 очку\n");
                shared->scores[i] += 1;
                shared->scores[j] += 1;
            } else if (res == 1) {
                printf("  Победил %d (+2)\n", i);
                send_to_observer("  Победил Игрок %d (+2 очка)\n", i);
                shared->scores[i] += 2;
            } else {
                printf("  Победил %d (+2)\n", j);
                send_to_observer("  Победил Игрок %d (+2 очка)\n", j);
                shared->scores[j] += 2;
            }
            sem_post(sem_output);
            sleep(1);
        }
    }
}

static void print_final_results(void) {
    sem_wait(sem_output);
    printf("\n=== Итоговая таблица ===\n");
    send_to_observer("\n=== Итоговая таблица ===\n");

    // Определяем структуру для результатов
    typedef struct {
        int id;
        int score;
    } PlayerResult;

    // Создаем массив для сортировки результатов
    PlayerResult results[N];

    for (int i = 0; i < N; i++) {
        results[i].id = i;
        results[i].score = shared->scores[i];
    }

    // Сортировка по убыванию очков
    for (int i = 0; i < N - 1; i++) {
        for (int j = i + 1; j < N; j++) {
            if (results[j].score > results[i].score) {
                // Меняем местами элементы
                PlayerResult temp = results[i];
                results[i] = results[j];
                results[j] = temp;
            }
        }
    }

    // Вывод результатов
    for (int i = 0; i < N; i++) {
        printf("%d место: Игрок %d - %d очков\n",
               i + 1, results[i].id, results[i].score);
        send_to_observer("%d место: Игрок %d - %d очков\n",
               i + 1, results[i].id, results[i].score);
    }

    sem_post(sem_output);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <количество_студентов>\n", argv[0]);
        return 1;
    }
    N = atoi(argv[1]);
    if (N < 2 || N > MAX_PLAYERS) {
        fprintf(stderr, "Ошибка: количество студентов 2..%d\n", MAX_PLAYERS);
        return 1;
    }

    // Установка обработчиков сигналов
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
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

    fprintf(stdout, "[MASTER] PID=%d, создаём ресурсы для N=%d\n", getpid(), N);

    // Удаляем старые семафоры/shm, если есть
    unlink_if_exists(SEM_OUTPUT_MUTEX);
    unlink_if_exists(SEM_TURN_COMPLETE);
    for (int i = 0; i < N; ++i) {
        char sname[64];
        build_start_name(i, sname, sizeof(sname));
        sem_unlink(sname);
    }
    shm_unlink(SHM_NAME);

    // Создаём shared memory
    fd_shm = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd_shm == -1) {
        perror("shm_open");
        return 1;
    }
    if (ftruncate(fd_shm, sizeof(SharedData)) == -1) {
        perror("ftruncate");
        close(fd_shm);
        shm_unlink(SHM_NAME);
        return 1;
    }
    shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        close(fd_shm);
        shm_unlink(SHM_NAME);
        return 1;
    }
    memset(shared, 0, sizeof(SharedData));
    shared->game_over = false;
    shared->total_players = N;

    // Создаём именованные семафоры
    sem_output = sem_open(SEM_OUTPUT_MUTEX, O_CREAT | O_EXCL, 0600, 1);
    if (sem_output == SEM_FAILED) {
        perror("sem_open output");
        cleanup_master();
        return 1;
    }

    sem_turn_complete = sem_open(SEM_TURN_COMPLETE, O_CREAT | O_EXCL, 0600, 0);
    if (sem_turn_complete == SEM_FAILED) {
        perror("sem_open turn_complete");
        cleanup_master();
        return 1;
    }

    for (int i = 0; i < N; ++i) {
        char sname[64];
        build_start_name(i, sname, sizeof(sname));
        sem_start[i] = sem_open(sname, O_CREAT | O_EXCL, 0600, 0);
        if (sem_start[i] == SEM_FAILED) {
            perror("sem_open sem_start");
            cleanup_master();
            return 1;
        }
    }

    // Ждём регистрации всех игроков
    wait_for_registration();

    // Запускаем турнир
    run_tournament();

    // Финальные результаты
    print_final_results();

    // Завершение
    shared->game_over = true;
    for (int i = 0; i < N; ++i) {
        if (sem_start[i]) {
            sem_post(sem_start[i]);
        }
    }

    // Даем время игрокам завершиться
    sleep(2);

    send_to_observer("[OBSERVER] Турнир завершен\n");
    sleep(1);

    cleanup_master();
    fprintf(stdout, "[MASTER] Завершил работу успешно\n");
    return 0;
}