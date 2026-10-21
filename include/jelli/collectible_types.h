#ifndef JELLI_COLLECTIBLE_TYPES_H
#define JELLI_COLLECTIBLE_TYPES_H
#include "jelli/prizes.h"

#define JELLI_CHAIN_GRID 8u
#define JELLI_CHAIN_LENGTH 7u
#define JELLI_COLLECTIBLE_PROGRESS_TICKS 600u
#define JELLI_COLLECTIBLE_CHAIN_PRIZE 10u
#define JELLI_COLLECTIBLE_RECORD_PRIZE 11u
#define JELLI_RECORD_RATE 22050u
#define JELLI_RECORD_SAMPLES 661500u

typedef enum {
    JELLI_CHAIN_UP,
    JELLI_CHAIN_RIGHT,
    JELLI_CHAIN_DOWN,
    JELLI_CHAIN_LEFT
} JelliChainDirection;
typedef enum { JELLI_CHAIN_RUNNING, JELLI_CHAIN_WON, JELLI_CHAIN_LOST } JelliChainStatus;
typedef enum {
    JELLI_COLLECTIBLE_GENERIC,
    JELLI_COLLECTIBLE_CHAIN,
    JELLI_COLLECTIBLE_RECORD
} JelliCollectibleMode;
typedef enum {
    JELLI_COLLECTIBLE_UP,
    JELLI_COLLECTIBLE_RIGHT,
    JELLI_COLLECTIBLE_DOWN,
    JELLI_COLLECTIBLE_LEFT,
    JELLI_COLLECTIBLE_PAUSE,
    JELLI_COLLECTIBLE_RESUME,
    JELLI_COLLECTIBLE_RESTART,
    JELLI_COLLECTIBLE_CLOSE,
    JELLI_COLLECTIBLE_NORMAL,
    JELLI_COLLECTIBLE_RELAXED
} JelliCollectibleControl;

/* Cells are y * GRID + x. Body[0] is the head. Pickup becomes a star at length7. */
typedef struct {
    uint8_t body[JELLI_CHAIN_LENGTH];
    uint8_t length, direction, pending_direction, pickup, status, step_ticks;
    bool turned, relaxed;
} JelliChain;

typedef struct {
    uint16_t play_ticks[JELLI_PRIZE_COUNT];
    uint16_t special_unlocked;
    bool record_rewarded;
} JelliCollectibleProgress;

/* One resumable checkpoint; opening another item/pet replaces it explicitly. */
typedef struct {
    JelliChain chain;
    uint32_t pet_id, record_cursor, generation;
    uint16_t elapsed_ticks;
    uint8_t prize, mode;
    bool active, paused;
} JelliCollectibleSession;
#endif
