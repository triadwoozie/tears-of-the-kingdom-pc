#include "game_patches_v143.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static GamePatchesState s_state = {
    .enabled = false,
    .uncapped = false,
    .target_hz = 30,
    .dt_seconds = 1.0f / 30.0f,
    .tick_scale_30 = 1.0f,
    .tick_scale_60 = 2.0f,
    .in_cutscene = false,
    .havok_substep_factor = 1.0f,
    .frame_count = 0,
    .last_time_ns = 0,
};

static bool s_initialized = false;
static uint64_t s_last_movie_tick_ns = 0;

static uint64_t get_time_ns_fallback(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void game_patches_v143_init(void) {
    const char *f60 = getenv("TOTK_FPS60");
    const char *patch_f60 = getenv("TOTK_PATCH_60FPS");
    const char *uncapped = getenv("TOTK_UNCAPPED");
    const char *uncapped_hz = getenv("TOTK_UNCAPPED_HZ");

    bool want_60 = (f60 && !strcmp(f60, "1")) || (patch_f60 && !strcmp(patch_f60, "1"));
    bool want_uncapped = (uncapped && !strcmp(uncapped, "1"));

    s_state.enabled = want_60 || want_uncapped;
    s_state.uncapped = want_uncapped;

    if (want_uncapped) {
        int hz = 120;
        if (uncapped_hz) {
            if (!strcmp(uncapped_hz, "display")) {
                hz = 120;
            } else {
                int parsed = atoi(uncapped_hz);
                if (parsed >= 30 && parsed <= 240) {
                    hz = parsed;
                }
            }
        }
        s_state.target_hz = hz;
    } else if (want_60) {
        s_state.target_hz = 60;
    } else {
        s_state.target_hz = 30;
    }

    s_state.dt_seconds = 1.0f / (float)s_state.target_hz;
    s_state.tick_scale_30 = s_state.dt_seconds * 30.0f;
    s_state.tick_scale_60 = s_state.dt_seconds * 60.0f;
    s_state.havok_substep_factor = (s_state.target_hz > 60) ? 0.5f : 1.0f;
    s_state.in_cutscene = false;
    s_state.frame_count = 0;
    s_state.last_time_ns = get_time_ns_fallback();
    s_initialized = true;
}

void game_patches_v143_step_frame(uint64_t now_ns) {
    if (!s_initialized) {
        game_patches_v143_init();
    }

    if (now_ns == 0) {
        now_ns = get_time_ns_fallback();
    }

    if (s_state.last_time_ns == 0) {
        s_state.last_time_ns = now_ns;
        return;
    }

    /* Auto-decay movie cutscene lock after 150ms without movie_host_tick */
    if (s_state.in_cutscene) {
        if (s_last_movie_tick_ns > 0 && (now_ns > s_last_movie_tick_ns) && (now_ns - s_last_movie_tick_ns > 150000000ull)) {
            s_state.in_cutscene = false;
        }
    }

    int64_t diff_ns = (int64_t)(now_ns - s_state.last_time_ns);
    s_state.last_time_ns = now_ns;
    s_state.frame_count++;

    /* Clamp dt between 1/240 s and 1/15 s to avoid simulation explosion */
    const float min_dt = 1.0f / 240.0f;
    const float max_dt = 1.0f / 15.0f;

    float raw_dt;
    if (diff_ns <= 0) {
        raw_dt = 1.0f / (float)s_state.target_hz;
    } else {
        raw_dt = (float)diff_ns / 1000000000.0f;
    }

    float dt = raw_dt < min_dt ? min_dt : (raw_dt > max_dt ? max_dt : raw_dt);

    if (s_state.in_cutscene) {
        /* Enforce fixed 30 FPS simulation rate during video/pre-rendered cutscenes */
        s_state.dt_seconds = 1.0f / 30.0f;
        s_state.tick_scale_30 = 1.0f;
        s_state.tick_scale_60 = 2.0f;
        s_state.havok_substep_factor = 1.0f;
    } else if (s_state.enabled) {
        s_state.dt_seconds = dt;
        s_state.tick_scale_30 = dt * 30.0f;
        s_state.tick_scale_60 = dt * 60.0f;

        /* Havok sub-stepping damping above 60 FPS to prevent joint constraint oscillations */
        if (dt < (1.0f / 60.0f)) {
            /* Damped physics delta scale for high-refresh rates (e.g. 120 FPS / 144 FPS) */
            float substep = 0.5f * (1.0f + (dt * 30.0f));
            s_state.havok_substep_factor = (substep < 0.25f) ? 0.25f : (substep > 1.0f ? 1.0f : substep);
        } else {
            s_state.havok_substep_factor = 1.0f;
        }
    } else {
        /* Fixed 30 FPS stock pacing */
        s_state.dt_seconds = 1.0f / 30.0f;
        s_state.tick_scale_30 = 1.0f;
        s_state.tick_scale_60 = 2.0f;
        s_state.havok_substep_factor = 1.0f;
    }
}

float game_patches_v143_get_dt(void) {
    return s_state.dt_seconds;
}

float game_patches_v143_get_tick_scale_30(void) {
    return s_state.tick_scale_30;
}

float game_patches_v143_get_tick_scale_60(void) {
    return s_state.tick_scale_60;
}

bool game_patches_v143_is_cutscene(void) {
    return s_state.in_cutscene;
}

void game_patches_v143_set_cutscene(bool playing) {
    s_state.in_cutscene = playing;
    if (playing) {
        s_last_movie_tick_ns = get_time_ns_fallback();
    }
}

const GamePatchesState *game_patches_v143_get_state(void) {
    return &s_state;
}

/* Hook definitions called from NVN QueuePresentTexture */
void runtime_frame_step_update(uint64_t now_ns) {
    game_patches_v143_step_frame(now_ns);
}

void movie_host_tick(void) {
    s_state.in_cutscene = true;
    s_last_movie_tick_ns = get_time_ns_fallback();
}
