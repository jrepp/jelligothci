#include "jelli/pet_engine.h"
#include "jelli/motion.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    uint64_t now;
    unsigned presents, polls;
    JelliInput pending;
    bool ready, repeat;
} Fake;

static uint64_t now_ms(void *context) { return ((Fake *)context)->now; }

static bool poll(void *context, JelliInput *input)
{
    Fake *fake = context;
    ++fake->polls;
    if (!fake->ready)
        return false;
    *input = fake->pending;
    fake->ready = fake->repeat;
    return true;
}

static void present(void *context, const JelliSurface *surface)
{
    CHECK(surface->pixels != NULL);
    ++((Fake *)context)->presents;
}

static void send(Fake *fake, JelliInputKind kind, int x, int y)
{
    fake->pending = (JelliInput){kind, x, y};
    fake->ready = true;
}

static void clock_pause_and_input(JelliSurface surface)
{
    Fake fake = {0};
    JelliPlatform platform = {&fake, now_ms, poll, present, NULL};
    JelliPetEngine engine;
    CHECK(jelli_pet_init(&engine, platform, surface));
    CHECK(jelli_pet_frame(&engine));
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_FOOD;
    send(&fake, JELLI_TAP, 110, 111);
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.game.pets[0].activity == JELLI_EATING);
    fake.now = 100u;
    CHECK(jelli_pet_frame(&engine) && engine.game.pets[0].ticks == 1u);
    send(&fake, JELLI_TOGGLE_PAUSE, 0, 0);
    CHECK(jelli_pet_frame(&engine) && engine.paused);
    fake.now = 1000u;
    CHECK(jelli_pet_frame(&engine) && engine.game.pets[0].ticks == 1u);
    send(&fake, JELLI_TOGGLE_PAUSE, 0, 0);
    CHECK(jelli_pet_frame(&engine) && !engine.paused);
    fake.now = 900u;
    CHECK(jelli_pet_frame(&engine) && engine.game.pets[0].ticks == 1u);
    send(&fake, JELLI_TAP, -1, -1);
    fake.repeat = true;
    fake.polls = 0;
    CHECK(jelli_pet_frame(&engine) && fake.polls == 32u);
    send(&fake, JELLI_QUIT, 0, 0);
    CHECK(!jelli_pet_frame(&engine));
}

static void resume_blocks_final_batch(JelliSurface surface)
{
    Fake fake = {0};
    JelliPlatform platform = {&fake, now_ms, poll, present, NULL};
    JelliPetEngine engine;
    CHECK(jelli_pet_init(&engine, platform, surface));
    engine.ui.menu_open = true;
    engine.ui.page = JELLI_UI_FOOD;
    jelli_game_resume_begin(&engine.game, 1000u);
    send(&fake, JELLI_TAP, 110, 111);
    CHECK(jelli_pet_frame(&engine));
    CHECK(!engine.game.resuming && engine.game.pets[0].activity == JELLI_IDLE);
    CHECK(engine.game.food == 5u);
    send(&fake, JELLI_TAP, 110, 111);
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.game.pets[0].activity == JELLI_EATING);
}

static void motion_devices(JelliSurface surface)
{
    Fake fake = {.now = 100};
    JelliPlatform platform = {&fake, now_ms, poll, present, NULL};
    JelliPetEngine engine;
    CHECK(jelli_pet_init(&engine, platform, surface));
    CHECK(jelli_pet_frame(&engine));
    CHECK(engine.motion_result == JELLI_DEVICE_UNAVAILABLE && !engine.motion.detected);
    JelliMotionMailbox box = {0};
    jelli_pet_bind_devices(&engine, (JelliMotionDriver){&box, jelli_motion_poll},
                           (JelliDisplayDriver){0});
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){100, true});
    CHECK(jelli_pet_frame(&engine) && engine.motion.detected);
    CHECK(engine.motion.observed_ms == 100 && engine.motion_result == JELLI_DEVICE_OK);
    CHECK(jelli_pet_frame(&engine) && !engine.motion.detected);
    jelli_motion_publish(&box, JELLI_DEVICE_OK, (JelliMotionSample){101, true});
    CHECK(jelli_pet_frame(&engine) && !engine.motion.detected);
    CHECK(engine.motion_result == JELLI_DEVICE_RESPONSE);
    jelli_motion_publish(&box, JELLI_DEVICE_TIMEOUT, (JelliMotionSample){0});
    CHECK(jelli_pet_frame(&engine) && engine.motion_result == JELLI_DEVICE_TIMEOUT);
    jelli_pet_bind_devices(&engine, (JelliMotionDriver){0}, (JelliDisplayDriver){0});
    CHECK(jelli_pet_frame(&engine) && engine.motion_result == JELLI_DEVICE_UNAVAILABLE);
}

int main(void)
{
    static uint16_t pixels[JELLI_WIDTH * JELLI_HEIGHT];
    JelliSurface surface = {pixels, JELLI_WIDTH, JELLI_HEIGHT, JELLI_WIDTH, {0}};
    motion_devices(surface);
    clock_pause_and_input(surface);
    resume_blocks_final_batch(surface);
    printf("Pet engine checks passed; game=%zu, pet-engine=%zu bytes\n", sizeof(JelliGame),
           sizeof(JelliPetEngine));
    return 0;
}
