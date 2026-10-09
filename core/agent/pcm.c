#include "jelli/agent_pcm.h"

void jelli_agent_pcm_join(JelliAgentPcmJoin *join, const uint8_t *data, size_t size,
                          bool message_start, JelliAgentPcmParts *parts)
{
    parts->joined[0] = parts->joined[1] = 0u;
    parts->joined_size = 0u;
    parts->body = data;
    parts->body_size = 0u;
    if (message_start)
        join->carrying = false; /* A new message never continues an old sample. */
    if (!size)
        return;
    if (join->carrying) {
        parts->joined[0] = join->carry;
        parts->joined[1] = data[0];
        parts->joined_size = 2u;
        join->carrying = false;
        ++data;
        --size;
    }
    if (size % 2u) {
        join->carry = data[size - 1u];
        join->carrying = true;
        --size;
    }
    parts->body = data;
    parts->body_size = size;
}
