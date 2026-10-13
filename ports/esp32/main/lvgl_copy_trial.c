#include "lvgl_copy_trial.h"
#include "pipeline_profile.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.h"
#include <stdatomic.h>
#include <string.h>

/* Other LVGL utility callers may run outside the display mutex. */
static atomic_bool use_libc;
static _Alignas(16) unsigned char source[320], target[320];
_Static_assert(sizeof(source) + sizeof(target) == 640u, "Copy qualification scratch budget");
void *__real_lv_memcpy(void *dest, const void *src, size_t bytes);
void *__wrap_lv_memcpy(void *dest, const void *src, size_t bytes);
void __real_lv_draw_sw_blend_image_to_rgb565(lv_draw_sw_blend_image_dsc_t *dsc);
void __wrap_lv_draw_sw_blend_image_to_rgb565(lv_draw_sw_blend_image_dsc_t *dsc);

void *__wrap_lv_memcpy(void *dest, const void *src, size_t bytes)
{
    if (!bytes)
        return dest;
    return atomic_load_explicit(&use_libc, memory_order_relaxed)
               ? memcpy(dest, src, bytes)
               : __real_lv_memcpy(dest, src, bytes);
}

void __wrap_lv_draw_sw_blend_image_to_rgb565(lv_draw_sw_blend_image_dsc_t *dsc)
{
    uint64_t start = jelli_profile_now();
    __real_lv_draw_sw_blend_image_to_rgb565(dsc);
    jelli_profile_record(JELLI_PROFILE_LVGL_IMAGE, start,
                         (uint32_t)dsc->dest_w * (uint32_t)dsc->dest_h * 2u);
}

void __real_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *dsc);
void __wrap_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *dsc);
void __wrap_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *dsc)
{
    uint64_t start = jelli_profile_now();
    __real_lv_draw_sw_blend_color_to_rgb565(dsc);
    jelli_profile_record(JELLI_PROFILE_LVGL_FILL, start,
                         (uint32_t)dsc->dest_w * (uint32_t)dsc->dest_h * 2u);
}

void jelli_lvgl_copy_mode(bool libc_copy)
{
    atomic_store_explicit(&use_libc, libc_copy, memory_order_relaxed);
}

static bool check_copy(unsigned from, unsigned to, unsigned count, bool libc_copy)
{
    memset(target, 0xa5, sizeof(target));
    const void *result = libc_copy ? memcpy(target + to, source + from, count)
                                   : __real_lv_memcpy(target + to, source + from, count);
    if (result != target + to)
        return false;
    for (unsigned i = 0; i < sizeof(target); ++i) {
        unsigned char expected = i >= to && i - to < count ? source[from + i - to] : 0xa5;
        if (target[i] != expected)
            return false;
    }
    return true;
}

bool jelli_lvgl_copy_qualify(void)
{
    static const unsigned sizes[] = {0, 1, 3, 4, 7, 15, 16, 17, 31, 32, 33, 257};
    for (unsigned i = 0; i < sizeof(source); ++i)
        source[i] = (unsigned char)(i * 37u + 19u);
    for (unsigned from = 0; from < 4u; ++from)
        for (unsigned to = 16; to < 20u; ++to)
            for (unsigned n = 0; n < sizeof(sizes) / sizeof(sizes[0]); ++n)
                if (!check_copy(from, to, sizes[n], false) || !check_copy(from, to, sizes[n], true))
                    return false;
    return true;
}
