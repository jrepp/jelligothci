#include "jelli/effect.h"
#include <stddef.h>

static bool interval_valid(unsigned start, unsigned duration, unsigned total)
{
    return duration > 0u && start < total && duration <= total - start;
}

static bool track_valid(const JelliEffectTrack *t, unsigned total)
{
    if (!interval_valid(t->start_ms, t->duration_ms, total) ||
        t->layer >= JELLI_EFFECT_LAYER_COUNT || t->anchor > JELLI_EFFECT_POINT_CAPACITY ||
        t->scale < 1u || t->scale > 8u || !t->clip.count ||
        t->clip.count > JELLI_CLIP_FRAME_CAPACITY)
        return false;
    if (t->x < -512 || t->x > 512 || t->y < -512 || t->y > 512 || t->end_x < -512 ||
        t->end_x > 512 || t->end_y < -512 || t->end_y > 512)
        return false;
    for (unsigned i = 0u; i < t->clip.count; ++i)
        if (!t->clip.frames[i] || !t->clip.durations_ms[i])
            return false;
    return true;
}

bool jelli_effect_valid(const JelliEffectDefinition *d)
{
    if (!d || !d->duration_ms || d->duration_ms > JELLI_EFFECT_DURATION_MAX ||
        d->track_count > JELLI_EFFECT_TRACK_CAPACITY || d->cue_count > JELLI_EFFECT_CUE_CAPACITY)
        return false;
    for (unsigned i = 0u; i < d->track_count; ++i)
        if (!track_valid(&d->tracks[i], d->duration_ms))
            return false;
    for (unsigned i = 0u; i < d->cue_count; ++i) {
        const JelliEffectCue *c = &d->cues[i];
        if (!interval_valid(c->start_ms, c->duration_ms, d->duration_ms) ||
            c->pose >= JELLI_POSE_COUNT)
            return false;
        for (unsigned j = 0u; j < i; ++j) {
            const JelliEffectCue *p = &d->cues[j];
            if (c->start_ms < p->start_ms + p->duration_ms &&
                p->start_ms < c->start_ms + c->duration_ms)
                return false;
        }
    }
    return true;
}

static int16_t interpolate(int16_t from, int16_t to, int32_t progress)
{
    return (int16_t)((int32_t)from + ((int32_t)to - from) * progress / 1024);
}

void jelli_effect_sample(const JelliEffectDefinition *d, uint64_t elapsed_ms,
                         JelliEffectSample *sample)
{
    if (!sample)
        return;
    *sample = (JelliEffectSample){0};
    if (!jelli_effect_valid(d) || elapsed_ms >= d->duration_ms)
        return;
    sample->active = true;
    unsigned time = (unsigned)elapsed_ms;
    for (unsigned i = 0u; i < d->cue_count; ++i) {
        const JelliEffectCue *c = &d->cues[i];
        if (time >= c->start_ms && time - c->start_ms < c->duration_ms) {
            sample->has_pose = true;
            sample->pose = c->pose;
            sample->cue_elapsed_ms = (uint16_t)(time - c->start_ms);
        }
    }
    for (unsigned i = 0u; i < d->track_count; ++i) {
        const JelliEffectTrack *t = &d->tracks[i];
        if (time < t->start_ms || time - t->start_ms >= t->duration_ms)
            continue;
        unsigned local = time - t->start_ms;
        int32_t p = (int32_t)(local * 1024u / t->duration_ms);
        if (t->ease)
            p = p * p / 1024 * (3072 - 2 * p) / 1024;
        unsigned frame = jelli_clip_frame(&t->clip, local);
        sample->sprites[sample->count++] = (JelliEffectSprite){t->clip.frames[frame],
                                                               interpolate(t->x, t->end_x, p),
                                                               interpolate(t->y, t->end_y, p),
                                                               t->layer,
                                                               t->anchor,
                                                               t->scale};
    }
}
