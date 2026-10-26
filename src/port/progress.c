/* Mission mode progress (progress.h). */
#include "progress.h"

#include <stdio.h>

#include "neogeo.h"
#include "profile.h"

static char file[1100];
static int unlocked = 1;
static int last = -1;                   /* the current mission seen at the previous frame */
static int completed, rush;

int progress_supported(void) { return game_profile.mission_now && game_profile.missions > 1 && profile_supported(); }
int progress_unlocked(void) { return unlocked; }
int progress_rush_unlocked(void) { return rush; }

int progress_take_completed(void)
{
    int c = completed;
    completed = 0;
    return c;
}

static void save(void);

void progress_unlock_rush(void)
{
    if (rush) return;
    rush = 1;
    save();
}

void progress_load(const char *path)
{
    snprintf(file, sizeof file, "%s", path);
    FILE *f = fopen(file, "r");
    if (!f) return;
    int n;
    char line[128];
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "unlocked=%d", &n) == 1 && n >= 1 && n <= (int)game_profile.missions) unlocked = n;
        else if (sscanf(line, "boss_rush=%d", &n) == 1) rush = n != 0;
    fclose(f);
}

static void save(void)
{
    FILE *f = file[0] ? fopen(file, "w") : NULL;
    if (!f) return;
    fprintf(f, "; %s (native port): mission mode progress.\n; unlocked: missions you can start at\n"
               "unlocked=%d\n; boss_rush: 1 once the game was completed in Arcade or Mission mode\nboss_rush=%d\n",
            ng_game->title, unlocked, rush);
    fclose(f);
}

int progress_frame(void)
{
    if (!progress_supported()) return 0;
    int now = ng.ram[game_profile.mission_now & 0xFFFF];
    int playing = ng.ram[0xFDB6] == 1 || ng.ram[0xFDB7] == 1;    /* BIOS player status */
    int was = last;
    last = now;
    /* The game's "next mission" step at the end of one (Metal Slug 3: 0xED704). The attract
       mode's demo uses its own values (8). */
    /* the last mission's step goes to `missions` when the final boss falls (docs/BOSS_RUSH.md) */
    if (playing && was == (int)game_profile.missions - 1 && now == (int)game_profile.missions) completed = 1;
    if (!playing || was < 0 || now != was + 1 || now >= (int)game_profile.missions) return 0;
    if (now + 1 <= unlocked) return 0;
    unlocked = now + 1;
    save();
    return unlocked;
}
