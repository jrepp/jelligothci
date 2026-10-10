#ifndef JELLI_LOCATIONS_H
#define JELLI_LOCATIONS_H
#include <stdbool.h>
#include <stdint.h>

#define JELLI_LOCATION_CAPACITY 8u
/* Stable catalog IDs are stored in saves; append locations, never reorder them. */
typedef struct {
    const char *name, *heading;
    uint32_t background;
    bool outdoors;
} JelliLocation;
extern const JelliLocation jelli_locations[];
extern const unsigned jelli_location_count;
#endif
