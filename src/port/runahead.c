/* Run-ahead (runahead.h). */
#include "runahead.h"

#include <stdlib.h>
#include <string.h>

#include "neogeo.h"

int runahead_frames(int n)
{
    if (n <= 0) return 0;
    if (n > RUNAHEAD_MAX) n = RUNAHEAD_MAX;
    size_t size;
    uint8_t *state = ng_state_save(&size);
    if (!state) return 0;
    /* the frames ahead have no effect outside the machine: no audio, no hooks (rumble, Boss Rush,
       the tests' fingerprints and timed inputs) */
    uint32_t hook_word = ng_hook_word;
    void (*on_vblank)(void) = ng_on_vblank_ack;
    void (*on_command)(uint8_t) = ng_on_sound_command;
    void (*before_read)(void) = ng_before_input_read;
    int audio = ng_audio_enabled;
    ng_hook_word = 0;
    ng_on_vblank_ack = NULL;
    ng_on_sound_command = NULL;
    ng_before_input_read = NULL;           /* the frames ahead hold the current inputs */
    ng_audio_enabled = 0;
    for (int i = 0; i < n; i++) ng_run_frame();
    static uint32_t ahead[NG_HEIGHT][NG_WIDTH];
    memcpy(ahead, ng.screen, sizeof ahead);
    int ok = ng_state_load(state, size) == 0;
    free(state);
    ng_hook_word = hook_word;
    ng_on_vblank_ack = on_vblank;
    ng_on_sound_command = on_command;
    ng_before_input_read = before_read;
    ng_audio_enabled = audio;
    if (ok) memcpy(ng.screen, ahead, sizeof ahead);
    return ok;
}
