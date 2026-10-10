#include "jelli/creature.h"

unsigned jelli_clip_frame(const JelliClip *clip, uint64_t elapsed_ms)
{
    if (clip == 0 || clip->count == 0u)
        return 0u;
    unsigned count =
        clip->count < JELLI_CLIP_FRAME_CAPACITY ? clip->count : JELLI_CLIP_FRAME_CAPACITY;
    uint64_t total = 0u;
    for (unsigned i = 0u; i < count; ++i)
        total += clip->durations_ms[i];
    if (total == 0u)
        return 0u;
    if (clip->loop)
        elapsed_ms %= total;
    else if (elapsed_ms >= total)
        return count - 1u;
    for (unsigned i = 0u; i < count; ++i) {
        if (elapsed_ms < clip->durations_ms[i])
            return i;
        elapsed_ms -= clip->durations_ms[i];
    }
    return count - 1u;
}

unsigned jelli_creature_idle_pose(const JelliCreatureProfile *profile, uint64_t beat,
                                  uint32_t pet_id)
{
    unsigned count = profile->idle_beat_count;
    if (count == 0u || count > JELLI_IDLE_BEAT_CAPACITY)
        return JELLI_POSE_IDLE;
    unsigned pose = profile->idle_beats[beat % count];
    bool gesture = pose == JELLI_POSE_CURIOUS || pose == JELLI_POSE_CONTENT;
    /* Some cycles stay quiet instead of repeating every gesture. */
    if (gesture && profile->quiet_cycle && (beat / count + pet_id) % profile->quiet_cycle == 0u)
        return JELLI_POSE_IDLE;
    return pose <= JELLI_POSE_CONTENT ? pose : JELLI_POSE_IDLE;
}
