#include "jelli/assets.h"

const JelliAsset *jelli_asset_lookup(const JelliAssetSet *set, uint32_t id)
{
    if (set && set->items)
        for (size_t i = 0u; i < set->count; ++i)
            if (set->items[i].id == id)
                return &set->items[i];
    return jelli_asset_find(id);
}

const uint8_t *jelli_asset_lookup_glyph(const JelliAssetSet *set, uint8_t codepoint)
{
    if (!set || !set->glyphs)
        return jelli_asset_glyph(codepoint);
    if (codepoint < 32u || codepoint > 127u)
        codepoint = (uint8_t)'?';
    return &set->glyphs[(size_t)(codepoint - 32u) * 12u];
}
