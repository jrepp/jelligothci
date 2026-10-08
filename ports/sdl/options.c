#include "options.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name)
{
    printf("Usage: %s [--pet|--shapes] [--save base-path] [--headless] [--frames N]\n"
           "       [--debug-socket path] [--snapshot file.bmp] [--demo] [--audio] [--wall-ms N]\n"
           "Pet mode is default. Click menus; Space debug-pauses; Escape exits.\n"
           "--save enables two-slot local persistence. Omit it for an unsaved session.\n"
           "--demo requires --headless and scripts care using 100 ms/frame.\n"
           "Other headless runs use 8 ms/frame. --wall-ms supplies a fake headless clock.\n",
           name);
}

static bool number(const char *text, uint64_t *out)
{
    char *end = NULL;
    if (!*text || *text == '-' || *text == '+')
        return false;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end || value > UINT64_MAX)
        return false;
    *out = (uint64_t)value;
    return true;
}

static bool value_option(const char *key, const char *value, JelliOptions *options)
{
    if (!strcmp(key, "--snapshot"))
        options->snapshot = value;
    else if (!strcmp(key, "--debug-socket"))
        options->debug_socket = value;
    else if (!strcmp(key, "--save"))
        options->save_path = value;
    else {
        uint64_t parsed = 0;
        if (!number(value, &parsed))
            return false;
        if (!strcmp(key, "--frames")) {
            if (!parsed || parsed > UINT_MAX)
                return false;
            options->max_frames = (unsigned long)parsed;
        } else if (!strcmp(key, "--wall-ms"))
            options->wall_ms = parsed;
        else
            return false;
    }
    return true;
}

int jelli_sdl_options(int argc, char **argv, JelliOptions *options)
{
    *options = (JelliOptions){.pet = true, .wall_ms = UINT64_C(1791331200000)};
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--headless"))
            options->headless = true;
        else if (!strcmp(arg, "--audio"))
            options->audio = true;
        else if (!strcmp(arg, "--pet"))
            options->pet = true;
        else if (!strcmp(arg, "--shapes"))
            options->pet = false;
        else if (!strcmp(arg, "--demo"))
            options->demo = true;
        else if (!strcmp(arg, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (i + 1 < argc && value_option(arg, argv[i + 1], options))
            ++i;
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if ((!options->pet && (options->save_path || options->demo || options->debug_socket)) ||
        (options->demo && !options->headless)) {
        usage(argv[0]);
        return 2;
    }
    if (options->headless && !options->max_frames && !options->debug_socket)
        options->max_frames = options->demo ? 750u : 1u;
    return -1;
}
