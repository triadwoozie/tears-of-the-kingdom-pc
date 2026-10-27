#ifndef ULTRATEARS_GAME_PATCHES_V143_H
#define ULTRATEARS_GAME_PATCHES_V143_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;             /* Real-time delta-time scaling active */
    bool uncapped;            /* Uncapped high-refresh rate mode */
    int target_hz;            /* 30, 60, 120, etc. */
    float dt_seconds;         /* Real elapsed frame duration clamped (1/240 .. 1/15) */
    float tick_scale_30;      /* Simulation step factor relative to 30 FPS base (e.g. 0.5 at 60fps) */
    float tick_scale_60;      /* Simulation step factor relative to 60 FPS target (e.g. 1.0 at 60fps) */
    bool in_cutscene;         /* Active video/movie playback flag (enforces 30Hz fixed lock) */
    float havok_substep_factor; /* Physics damping & sub-stepping above 60 FPS */
    uint64_t frame_count;
    uint64_t last_time_ns;
} GamePatchesState;

void game_patches_v143_init(void);
void game_patches_v143_step_frame(uint64_t now_ns);
float game_patches_v143_get_dt(void);
float game_patches_v143_get_tick_scale_30(void);
float game_patches_v143_get_tick_scale_60(void);
bool game_patches_v143_is_cutscene(void);
void game_patches_v143_set_cutscene(bool playing);
const GamePatchesState *game_patches_v143_get_state(void);

/* Weak hook callbacks for NVN / host present link */
void runtime_frame_step_update(uint64_t now_ns);
void movie_host_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* ULTRATEARS_GAME_PATCHES_V143_H */
