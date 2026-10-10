#include "jelli/activities.h"

unsigned jelli_moment_suggested(unsigned hour)
{
    /* The latest window starting at or before hour; before the first window, the last
     * window of the previous day still applies. */
    unsigned best = 0u, best_hour = 0u, last = 0u, last_hour = 0u;
    bool found = false, any = false;
    for (unsigned i = 0u; i < jelli_moment_count && i < JELLI_MOMENT_CAPACITY; ++i) {
        unsigned start = jelli_moments[i].suggest_hour;
        if (start > 23u)
            continue;
        if (!any || start >= last_hour) {
            last = i;
            last_hour = start;
            any = true;
        }
        if (start <= hour % 24u && (!found || start >= best_hour)) {
            best = i;
            best_hour = start;
            found = true;
        }
    }
    return found ? best : last;
}
