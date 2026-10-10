#include "ui_art.h"
#include "jelli/agent_scene.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>

enum {
    MARGIN = 10,   /* Panel edge to the home unicorn's farthest point, before its float. */
    POSE_A = 1041, /* creatures.unicorn-idle-a */
    POSE_B = 1042, /* creatures.unicorn-idle-b, a gentle squash of A */
    PRANCE = 13001 /* agent.unicorn-prance-a, the first of the eight prancing frames */
};

static const char *TAG = "agent_ui";

static void release(JelliAgentSprite *sprites, unsigned count)
{
    for (unsigned i = 0u; i < count; ++i) {
        heap_caps_free(sprites[i].pixels);
        heap_caps_free(sprites[i].alpha);
        sprites[i] = (JelliAgentSprite){NULL, NULL, 0u, 0u};
    }
}

/* Resamples crop of each asset into sprites of width_px x height_px. */
static bool fill(JelliAgentSprite *sprites, const JelliAsset *const *art, unsigned count,
                 JelliRect crop, float width_px, float height_px)
{
    if (!(width_px >= 1.0f && height_px >= 1.0f && width_px <= (float)JELLI_UI_SIDE &&
          height_px <= (float)JELLI_UI_SIDE))
        return false;
    unsigned width = (unsigned)lrintf(width_px), height = (unsigned)lrintf(height_px);
    for (unsigned i = 0u; i < count; ++i) {
        sprites[i] = (JelliAgentSprite){
            heap_caps_malloc((size_t)width * height * sizeof(uint16_t), MALLOC_CAP_SPIRAM),
            heap_caps_malloc((size_t)width * height, MALLOC_CAP_SPIRAM), width, height};
        if (!sprites[i].pixels || !sprites[i].alpha ||
            !jelli_agent_sprite_resample(&sprites[i], art[i], crop))
            return false;
    }
    return true;
}

/* Both idle poses from one crop, sized so the farther-reaching one, centred on the
 * panel, stays MARGIN inside its round edge. */
static bool prepare_home(JelliUiArt *ui)
{
    const JelliAsset *art[2] = {jelli_asset_find(POSE_A), jelli_asset_find(POSE_B)};
    if (!art[0] || !art[1])
        return false;
    JelliRect crop = jelli_agent_rect_union(jelli_agent_asset_content(art[0]),
                                            jelli_agent_asset_content(art[1]));
    float reach =
        fmaxf(jelli_agent_asset_reach(art[0], crop), jelli_agent_asset_reach(art[1], crop));
    if (!crop.width || reach <= 0.0f)
        return false;
    float scale = ((float)JELLI_UI_SIDE / 2.0f - (float)MARGIN) / reach;
    ESP_LOGI(TAG, "Home unicorn: scale %.2f of the art", (double)scale);
    return fill(ui->home, art, 2u, crop, (float)crop.width * scale, (float)crop.height * scale);
}

/* The prancing frames from the union of their contents, so they keep their places. */
static bool prepare_prance(JelliUiArt *ui)
{
    const JelliAsset *art[JELLI_PRANCE_FRAMES];
    JelliRect crop = {0u, 0u, 0u, 0u};
    for (unsigned i = 0u; i < JELLI_PRANCE_FRAMES; ++i) {
        art[i] = jelli_asset_find(PRANCE + i);
        if (!art[i] || art[i]->width != art[0]->width || art[i]->density != art[0]->density)
            return false;
        crop = jelli_agent_rect_union(crop, jelli_agent_asset_content(art[i]));
    }
    float stored = (float)art[0]->width * (float)art[0]->density;
    float scale = (float)JELLI_AGENT_PRANCE_FRAME_PX / stored;
    if (!crop.width || !fill(ui->prance, art, JELLI_PRANCE_FRAMES, crop, (float)crop.width * scale,
                             (float)crop.height * scale))
        return false;
    ui->x = (int)lrintf((float)crop.x * scale);
    ui->y = (int)lrintf((float)crop.y * scale);
    ui->mirror_x = JELLI_AGENT_PRANCE_FRAME_PX - ui->x - (int)ui->prance[0].width;
    return true;
}

bool jelli_agent_ui_art_prepare(JelliUiArt *art)
{
    if (prepare_home(art) && prepare_prance(art)) {
        ESP_LOGI(TAG, "Unicorn ready: home %ux%u px, prancing %ux%u px", art->home[0].width,
                 art->home[0].height, art->prance[0].width, art->prance[0].height);
        return true;
    }
    release(art->home, 2u);
    release(art->prance, JELLI_PRANCE_FRAMES);
    return false;
}
