#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#define MAX_PLAYERS 10
#define MAX_OBSERVERS 10
#define SHM_NAME "/rps_shm_tournament"

// Семафоры
#define SEM_OUTPUT_MUTEX "/rps_sem_output"
#define SEM_TURN_COMPLETE "/rps_sem_turn_done"
#define SEM_START_BASE "/rps_sem_start_"
#define SEM_OBSERVER_MUTEX "/rps_sem_observer"

// Каталог для FIFO наблюдателей
#define OBSERVERS_DIR "/tmp/rps_observers"

typedef enum { ROCK = 0, SCISSORS = 1, PAPER = 2 } Move;
extern const char *STR_MOVES[];

typedef struct {
    int current_moves[MAX_PLAYERS];
    int scores[MAX_PLAYERS];
    bool game_over;
    int matches_played;
    int total_matches;
    volatile int registered_players;
    int total_players;
} SharedData;

typedef struct {
    char fifo_name[256];
    pid_t pid;
} ObserverInfo;

// Вспомогательная функция для построения имен семафоров
static inline void build_start_name(int id, char *buf, size_t len) {
    snprintf(buf, len, "%s%d", SEM_START_BASE, id);
}

#endif // COMMON_H