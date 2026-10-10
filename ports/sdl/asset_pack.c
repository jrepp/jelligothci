#include "asset_reload.h"
#include <string.h>

static uint16_t u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u));
}

static uint32_t u32(const uint8_t *bytes)
{
    return (uint32_t)u16(bytes) | (uint32_t)u16(bytes + 2u) << 16u;
}

static bool metadata(const JelliAsset *asset, const JelliAsset *original)
{
    return original && asset->width == original->width && asset->height == original->height &&
           asset->left < asset->right && asset->top < asset->bottom &&
           asset->right <= asset->width && asset->bottom <= asset->height &&
           asset->ground_x_q8 <= (uint32_t)asset->width * 256u &&
           asset->ground_y_q8 <= (uint32_t)asset->height * 256u &&
           asset->centroid_x_q8 <= (uint32_t)asset->width * 256u &&
           asset->centroid_y_q8 <= (uint32_t)asset->height * 256u;
}

static bool geometry(const JelliAsset *asset)
{
    unsigned left = asset->width, top = asset->height, right = 0u, bottom = 0u;
    uint32_t count = 0u, sum_x = 0u, sum_y = 0u, band_count = 0u, band_x = 0u;
    for (unsigned y = 0u; y < asset->height; ++y) {
        for (unsigned x = 0u; x < asset->width; ++x) {
            if (!(asset->mask[y * asset->mask_stride + x / 8u] & (0x80u >> (x % 8u))))
                continue;
            left = x < left ? x : left;
            top = y < top ? y : top;
            right = x + 1u > right ? x + 1u : right;
            bottom = y + 1u > bottom ? y + 1u : bottom;
            ++count;
            sum_x += x * 2u + 1u;
            sum_y += y * 2u + 1u;
            if (y + 3u >= asset->bottom) {
                ++band_count;
                band_x += x * 2u + 1u;
            }
        }
    }
    if (!count || left != asset->left || top != asset->top || right != asset->right ||
        bottom != asset->bottom || (sum_x * 128u + count / 2u) / count != asset->centroid_x_q8 ||
        (sum_y * 128u + count / 2u) / count != asset->centroid_y_q8)
        return false;
    if (jelli_asset_find(asset->id)->ground_y_q8)
        return band_count && asset->ground_y_q8 == bottom * 256u &&
               asset->ground_x_q8 == (band_x * 128u + band_count / 2u) / band_count;
    return asset->ground_x_q8 == 0u && asset->ground_y_q8 == 0u;
}

static bool payload(JelliAssetBank *bank, JelliAsset *asset, const uint8_t *bytes, size_t size,
                    size_t *pixels, size_t *masks)
{
    if (asset->id == 4001u) {
        if (size != sizeof(bank->glyphs))
            return false;
        memcpy(bank->glyphs, bytes, size);
        bank->set.glyphs = bank->glyphs;
        return true;
    }
    size_t count = (size_t)asset->width * asset->height;
    size_t stride = ((size_t)asset->width + 7u) / 8u;
    size_t mask_size = stride * asset->height;
    if (size != count * 2u + mask_size ||
        count > sizeof(bank->pixels) / sizeof(bank->pixels[0]) - *pixels ||
        mask_size > sizeof(bank->masks) - *masks || stride > UINT8_MAX)
        return false;
    asset->pixels = bank->pixels + *pixels;
    asset->mask = bank->masks + *masks;
    asset->mask_stride = (uint8_t)stride;
    for (size_t i = 0u; i < count; ++i)
        bank->pixels[*pixels + i] = u16(bytes + i * 2u);
    memcpy(bank->masks + *masks, bytes + count * 2u, mask_size);
    *pixels += count;
    *masks += mask_size;
    return geometry(asset);
}

bool jelli_sdl_asset_pack_decode(JelliAssetBank *bank, const uint8_t *bytes, size_t size)
{
    if (!bank || !bytes || size < 16u || size > JELLI_ASSET_PACK_CAPACITY ||
        memcmp(bytes, "JLAP", 4u) || u32(bytes + 4u) != 1u)
        return false;
    uint32_t count = u32(bytes + 8u);
    if (!count || count > JELLI_ASSET_PACK_COUNT)
        return false;
    bank->set = (JelliAssetSet){.items = bank->descriptors, .count = count};
    size_t at = 16u, pixels = 0u, masks = 0u;
    for (unsigned i = 0u; i < count; ++i) {
        if (size - at < 24u)
            return false;
        const uint8_t *record = bytes + at;
        JelliAsset *asset = &bank->descriptors[i];
        *asset = (JelliAsset){.id = u32(record),
                              .width = u16(record + 4u),
                              .height = u16(record + 6u),
                              .ground_x_q8 = u16(record + 8u),
                              .ground_y_q8 = u16(record + 10u),
                              .centroid_x_q8 = u16(record + 12u),
                              .centroid_y_q8 = u16(record + 14u),
                              .left = record[16],
                              .top = record[17],
                              .right = record[18],
                              .bottom = record[19]};
        uint32_t length = u32(record + 20u);
        at += 24u;
        if (length > size - at || !metadata(asset, jelli_asset_find(asset->id)))
            return false;
        for (unsigned previous = 0u; previous < i; ++previous) {
            if (bank->descriptors[previous].id == asset->id)
                return false;
        }
        if (!payload(bank, asset, bytes + at, length, &pixels, &masks))
            return false;
        at += length;
    }
    return at == size;
}
