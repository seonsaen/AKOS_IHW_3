#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>

#define MAX_PLAYERS 10
#define SHM_NAME "/rps_tournament_shm"

typedef enum { ROCK, SCISSORS, PAPER } Move;
const char *STR_MOVES[] = {"Камень", "Ножницы", "Бумага"};

typedef struct {
    int current_moves[MAX_PLAYERS];
    int scores[MAX_PLAYERS];
    bool game_over;
    int matches_played;
    int total_matches;

    sem_t sem_start[MAX_PLAYERS];
    sem_t sem_turn_complete;
    sem_t output_mutex;
    pid_t owner_pid;
} SharedData;

SharedData *shared = NULL;
int player_id = -1;
pid_t children_pids[MAX_PLAYERS];
int N;
int fd_shm = -1;

void cleanup() {
    pid_t current_pid = getpid();

    if (shared && shared != MAP_FAILED) {
        pid_t owner = shared->owner_pid;

        // Все процессы отключаются от разделяемой памяти
        munmap(shared, sizeof(SharedData));
        shared = NULL;

        // Только владелец удаляет системные объекты
        if (current_pid == owner) {
            printf("\n[Владелец PID %d] Уничтожение семафоров и разделяемой памяти\n", current_pid);

            if (fd_shm != -1) {
                close(fd_shm);
                shm_unlink(SHM_NAME);
                fd_shm = -1;
            }
        }
    }
}

void sigint_handler() {
    printf("\n[PID %d] Получен SIGINT - завершение турнира\n", getpid());

    if (shared && shared != MAP_FAILED) {
        pid_t owner = shared->owner_pid;

        if (getpid() == owner) {
            printf("[Владелец] Завершение дочерних процессов...\n");
            shared->game_over = true;

            // Будим всех студентов для завершения
            for (int i = 0; i < N; i++) {
                sem_post(&shared->sem_start[i]);
            }

            // Ждем завершения детей
            for (int i = 0; i < N; i++) {
                if (children_pids[i] > 0) {
                    waitpid(children_pids[i], NULL, 0);
                }
            }
        }
    }

    cleanup();
    exit(0);
}

int judge(int m1, int m2) {
    if (m1 == m2) return 0; // Ничья
    if ((m1 == ROCK && m2 == SCISSORS) ||
        (m1 == SCISSORS && m2 == PAPER) ||
        (m1 == PAPER && m2 == ROCK)) return 1; // Первый победил
    return 2; // Второй победил
}

void child_logic(int id) {
    player_id = id;
    srand(time(NULL) ^ (getpid() << 16) ^ id);

    printf("[Студент %d] Готов к турниру (PID: %d)\n", id, getpid());

    while (1) {
        if (sem_wait(&shared->sem_start[id]) == -1) {
            if (errno == EINTR) continue;
            perror("sem_wait");
            break;
        }

        if (shared->game_over) {
            printf("[Студент %d] Завершает участие в турнире\n", id);
            break;
        }

        // Генерация хода
        int move = rand() % 3;
        shared->current_moves[id] = move;

        // Синхронизированный вывод
        sem_wait(&shared->output_mutex);
        printf("  Студент %d показывает: %s\n", id, STR_MOVES[move]);
        sem_post(&shared->output_mutex);

        // Сигнализируем о завершении хода
        sem_post(&shared->sem_turn_complete);
    }

    cleanup();
    _exit(0);
}

void init_shared_memory() {
    // Создание разделяемой памяти
    fd_shm = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd_shm == -1) {
        perror("shm_open");
        exit(1);
    }

    if (ftruncate(fd_shm, sizeof(SharedData)) == -1) {
        perror("ftruncate");
        close(fd_shm);
        exit(1);
    }

    shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        close(fd_shm);
        exit(1);
    }
}

void init_semaphores() {
    // Инициализация семафоров
    if (sem_init(&shared->sem_turn_complete, 1, 0) == -1) {
        perror("sem_init sem_turn_complete");
        cleanup();
        exit(1);
    }

    if (sem_init(&shared->output_mutex, 1, 1) == -1) {
        perror("sem_init output_mutex");
        cleanup();
        exit(1);
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sem_init(&shared->sem_start[i], 1, 0) == -1) {
            perror("sem_init sem_start");
            cleanup();
            exit(1);
        }
    }
}

void create_children(int n_players) {
    for (int i = 0; i < n_players; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Дочерний процесс
            child_logic(i);
        } else if (pid < 0) {
            perror("fork");
            // Убиваем уже созданные процессы при ошибке
            for (int j = 0; j < i; j++) {
                if (children_pids[j] > 0) {
                    kill(children_pids[j], SIGTERM);
                }
            }
            cleanup();
            exit(1);
        } else {
            children_pids[i] = pid;
        }
    }
}

void run_tournament(int n_players) {
    int total_matches = n_players * (n_players - 1) / 2;
    shared->total_matches = total_matches;
    shared->matches_played = 0;

    printf("\n=== Начало турнира! Всего матчей: %d ===\n", total_matches);

    for (int i = 0; i < n_players; i++) {
        for (int j = i + 1; j < n_players; j++) {
            shared->matches_played++;

            sem_wait(&shared->output_mutex);
            printf("\n--- Матч %d/%d: Студент %d vs Студент %d ---\n",
                   shared->matches_played, total_matches, i, j);
            sem_post(&shared->output_mutex);

            // Запускаем ходы студентов
            sem_post(&shared->sem_start[i]);
            sem_post(&shared->sem_start[j]);

            // Ждем завершения обоих ходов
            sem_wait(&shared->sem_turn_complete);
            sem_wait(&shared->sem_turn_complete);

            // Определяем результат
            int move1 = shared->current_moves[i];
            int move2 = shared->current_moves[j];
            int result = judge(move1, move2);

            // Вывод результата
            sem_wait(&shared->output_mutex);
            printf("  Ходы: %s vs %s\n", STR_MOVES[move1], STR_MOVES[move2]);

            if (result == 0) {
                printf("  Результат: НИЧЬЯ (+1 каждому)\n");
                shared->scores[i] += 1;
                shared->scores[j] += 1;
            } else if (result == 1) {
                printf("  Результат: ПОБЕДА Студента %d (+2 очка)\n", i);
                shared->scores[i] += 2;
            } else {
                printf("  Результат: ПОБЕДА Студента %d (+2 очка)\n", j);
                shared->scores[j] += 2;
            }
            sem_post(&shared->output_mutex);

            usleep(800000); // Задержка для наглядности
        }
    }
}

void print_final_results(int n_players) {
    printf("\n=== ТУРНИР ЗАВЕРШЕН! ИТОГОВАЯ ТАБЛИЦА ===\n");

    // Определяем структуру для результатов
    typedef struct {
        int id;
        int score;
    } PlayerResult;

    // Создаем массив для сортировки результатов
    PlayerResult results[MAX_PLAYERS];

    for (int i = 0; i < n_players; i++) {
        results[i].id = i;
        results[i].score = shared->scores[i];
    }

    // Сортировка по убыванию очков
    for (int i = 0; i < n_players - 1; i++) {
        for (int j = i + 1; j < n_players; j++) {
            if (results[j].score > results[i].score) {
                // Меняем местами элементы
                PlayerResult temp = results[i];
                results[i] = results[j];
                results[j] = temp;
            }
        }
    }

    // Вывод результатов
    for (int i = 0; i < n_players; i++) {
        printf("%d место: Студент %d - %d очков\n",
               i + 1, results[i].id, results[i].score);
    }
}

int main(int argc, char *argv[]) {
    // Инициализация массива PID'ов
    memset(children_pids, 0, sizeof(children_pids));

    // Парсинг аргументов командной строки
    N = 7; // значение по умолчанию
    if (argc == 2) {
        N = atoi(argv[1]);
    }
    if (N < 2 || N > MAX_PLAYERS) {
        fprintf(stderr, "Ошибка: количество студентов должно быть от 2 до %d\n", MAX_PLAYERS);
        return 1;
    }

    printf("=== Турнир 'Камень, Ножницы, Бумага' ===\n");
    printf("Количество студентов: %d\n", N);

    // Инициализация разделяемой памяти
    init_shared_memory();

    // Инициализация структуры данных
    memset(shared, 0, sizeof(SharedData));
    shared->owner_pid = getpid();
    shared->game_over = false;

    // Инициализация семафоров
    init_semaphores();

    // Установка обработчика сигналов
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Создание дочерних процессов
    create_children(N);

    // Даем время всем процессам инициализироваться
    sleep(2);

    // Запуск турнира
    run_tournament(N);

    // Завершение турнира
    shared->game_over = true;
    for (int i = 0; i < N; i++) {
        sem_post(&shared->sem_start[i]);
    }

    // Ожидание завершения всех дочерних процессов
    for (int i = 0; i < N; i++) {
        if (children_pids[i] > 0) {
            waitpid(children_pids[i], NULL, 0);
        }
    }

    // Вывод финальных результатов
    print_final_results(N);

    // Очистка ресурсов
    cleanup();

    printf("\nПрограмма завершена успешно!\n");
    return 0;
}