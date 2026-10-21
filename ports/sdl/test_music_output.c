#define SDL_MAIN_HANDLED
#include "sound_output.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    SDL_SetMainReady();
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    CHECK(jelli_sdl_sound_open());
    CHECK(jelli_sdl_music_control(
        NULL, (JelliMusicCommand){
                  .action = JELLI_MUSIC_START, .cursor = 1000, .serial = 1, .volume = 0}));
    JelliMusicStatus status = {0};
    for (unsigned i = 0; i < 100u; ++i) {
        CHECK(jelli_sdl_music_status(NULL, &status));
        if (status.cursor > 1000u)
            break;
        SDL_Delay(2);
    }
    CHECK(status.cursor > 1000 && status.serial == 1 && status.playing);
    CHECK(jelli_sdl_music_control(NULL,
                                  (JelliMusicCommand){.action = JELLI_MUSIC_PAUSE, .serial = 2}));
    CHECK(jelli_sdl_music_status(NULL, &status));
    uint32_t paused = status.cursor;
    SDL_Delay(50);
    CHECK(jelli_sdl_music_status(NULL, &status));
    CHECK(status.cursor == paused && !status.playing && status.serial == 2);
    CHECK(jelli_sdl_sound_request(NULL, JELLI_SOUND_COO, 40));
    CHECK(jelli_sdl_music_control(NULL, (JelliMusicCommand){.action = JELLI_MUSIC_START,
                                                            .cursor = JELLI_MUSIC_SAMPLES - 100,
                                                            .serial = 3,
                                                            .volume = 80}));
    for (unsigned i = 0; i < 100u; ++i) {
        CHECK(jelli_sdl_music_status(NULL, &status));
        if (status.finished)
            break;
        SDL_Delay(2);
    }
    CHECK(status.finished && !status.playing && status.serial == 3);
    CHECK(status.cursor == JELLI_MUSIC_SAMPLES);
    jelli_sdl_sound_close();
    CHECK(!jelli_sdl_music_status(NULL, &status));
    CHECK(!jelli_sdl_music_control(NULL, (JelliMusicCommand){.action = JELLI_MUSIC_START}));
    SDL_Quit();
    puts("Dummy SDL audio cursor, pause, mute, completion and care coexistence passed");
    return 0;
}
