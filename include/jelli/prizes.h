#ifndef JELLI_PRIZES_H
#define JELLI_PRIZES_H
#include <stdbool.h>
#include <stdint.h>
#define JELLI_PRIZE_COUNT 16u
/* Storage capacity follows the serialized uint16 ownership masks. Catalog IDs append only. */
typedef struct {
    const char *key;
    const char *name;
    uint16_t asset;
} JelliPrizeDefinition;
extern const JelliPrizeDefinition jelli_prizes[];
extern const unsigned jelli_prize_count;
#define JELLI_PRIZE_MASK ((uint16_t)((UINT32_C(1) << jelli_prize_count) - 1u))
/* Prizes saved in the original blocks; later prizes are saved in the version 14 tail. */
#define JELLI_PRIZE_LEGACY_COUNT 9u
typedef enum {
    JELLI_PRIZE_BRUSH,
    JELLI_PRIZE_WASH,
    JELLI_PRIZE_BREAKFAST,
    JELLI_PRIZE_TEA,
    JELLI_PRIZE_MOVIE,
    JELLI_PRIZE_OUTING,
    JELLI_PRIZE_CARE,
    JELLI_PRIZE_SLEEP,
    JELLI_PRIZE_TRAVEL,
    JELLI_PRIZE_GIFT
} JelliPrizeTrigger;
typedef struct {
    uint16_t counts[JELLI_PRIZE_LEGACY_COUNT];
    uint64_t last_breakfast_day;
    bool breakfast_day_known;
} JelliPrizeProgress;
typedef struct {
    uint16_t owned, discovered;
    uint32_t origin_pet[JELLI_PRIZE_COUNT];
    uint8_t offered; /* Zero or one-based prize; one pending catch at a time. */
    uint32_t offered_pet;
} JelliPrizes;
bool jelli_prizes_valid(const JelliPrizes *prizes);
bool jelli_prize_progress_valid(const JelliPrizeProgress *progress);
#endif
