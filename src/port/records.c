/* Records (records.h). */
#include "records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "dispatcher.h"
#include "neogeo.h"
#include "profile.h"
#include "progress.h"

#define MAX_MISSIONS 8
/* A mission takes minutes: a step sooner than this is Mission mode's jump at the start of a game
   (the game starts at mission 1 and loads the chosen one a moment later), not a completion. */
#define MIN_MISSION_FRAMES 3600

static struct { uint32_t frames, score; int has; } best[MAX_MISSIONS];
static uint32_t arcade_score;
static int arcade_mission, arcade_has;
static uint32_t rush_frames;
static int rush_defeated;
static char file[1100];

/* the mission being timed */
static int in_game_was, cur = -1, clean, turbo_used;
static uint64_t start_frame;
static uint32_t start_score;

int records_supported(void) { return progress_supported() && game_profile.score_p1 != 0; }

static uint32_t score_now(void)
{
    uint32_t s = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t b = ng.ram[(game_profile.score_p1 + (uint32_t)i) & 0xFFFF];
        s = s * 100 + (uint32_t)(b >> 4) * 10 + (b & 15);
    }
    return s;
}

static int fair_now(void) { return !cheats_dips && !cheats_devmode && !cpu_turbo; }

void records_load(const char *path)
{
    snprintf(file, sizeof file, "%s", path);
    FILE *f = fopen(file, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        int m;
        unsigned a, b;
        if (sscanf(line, "mission%d=%u,%u", &m, &a, &b) == 3 && m >= 1 && m <= MAX_MISSIONS && a) {
            best[m - 1].frames = a;
            best[m - 1].score = b;
            best[m - 1].has = 1;
        } else if (sscanf(line, "rush=%u,%u", &a, &b) == 2) {
            rush_frames = a;
            rush_defeated = (int)b;
        } else if (sscanf(line, "arcade=%u,%u", &a, &b) == 2) {
            arcade_score = a;
            arcade_mission = (int)b;
            arcade_has = 1;
        }
    }
    fclose(f);
}

static void save(void)
{
    FILE *f = file[0] ? fopen(file, "w") : NULL;
    if (!f) return;
    fprintf(f, "; %s (native port): records.\n; missionN=best time in frames (59.1856 per second),best score in the mission\n"
               "; arcade=best score,furthest mission\n", ng_game->title);
    for (int m = 0; m < MAX_MISSIONS; m++)
        if (best[m].has) fprintf(f, "mission%d=%u,%u\n", m + 1, (unsigned)best[m].frames, (unsigned)best[m].score);
    if (arcade_has) fprintf(f, "arcade=%u,%d\n", (unsigned)arcade_score, arcade_mission);
    if (rush_defeated) fprintf(f, "; rush=best clear time in frames (0: none),most bosses defeated\nrush=%u,%d\n",
                               (unsigned)rush_frames, rush_defeated);
    fclose(f);
}

static void begin(int mission)
{
    cur = mission;
    start_frame = ng.frames;
    start_score = score_now();
    clean = fair_now();
    turbo_used = cpu_turbo != 0;
}

void records_invalidate(void) { clean = 0; }

void records_reset_tracking(void)
{
    in_game_was = 0;
    cur = -1;
}

int records_frame(RecordEvent *e)
{
    if (!records_supported()) return 0;
    uint8_t s1 = ng.ram[0xFDB6], s2 = ng.ram[0xFDB7];               /* BIOS player status */
    int in_game = s1 == 1 || s1 == 2 || s2 == 1 || s2 == 2;          /* playing or continue countdown */
    int now = ng.ram[game_profile.mission_now & 0xFFFF];
    int was = in_game_was;
    in_game_was = in_game;
    if (!fair_now()) clean = 0;
    if (cpu_turbo) turbo_used = 1;
    if (!in_game) { cur = -1; return 0; }
    if (!was || cur < 0) {                                            /* a new game */
        if (now < (int)game_profile.missions) begin(now);
        return 0;
    }
    if (now == cur) return 0;
    uint64_t frames = ng.frames - start_frame;
    /* The last mission's step goes to `missions` (Metal Slug 3: 4 -> 5 when the final boss falls,
       then the ending; docs/BOSS_RUSH.md); the attract mode's demo uses other values (8). */
    /* PD_RECORDS_MIN_FRAMES (tests): a shorter minimum, to time a mission started near its end. */
    uint64_t min = getenv("PD_RECORDS_MIN_FRAMES") ? strtoull(getenv("PD_RECORDS_MIN_FRAMES"), NULL, 10) : MIN_MISSION_FRAMES;
    if (now != cur + 1 || now > (int)game_profile.missions || frames < min) {
        if (now < (int)game_profile.missions) begin(now);             /* Mission mode's jump */
        else cur = -1;
        return 0;
    }
    /* mission cur completed */
    memset(e, 0, sizeof *e);
    e->mission = cur + 1;
    e->frames = (uint32_t)frames;
    uint32_t sc = score_now();
    e->score = sc >= start_score ? sc - start_score : 0;
    e->clean = clean;
    e->turbo = turbo_used;
    if (clean && cur < MAX_MISSIONS) {
        if (!best[cur].has || e->frames < best[cur].frames) { e->new_time = 1; best[cur].frames = e->frames; }
        if (!best[cur].has || e->score > best[cur].score) { e->new_score = 1; best[cur].score = e->score; }
        best[cur].has = 1;
        save();
    }
    if (now < (int)game_profile.missions) begin(now);
    else cur = -1;                                                    /* the ending */
    return 1;
}

int records_arcade(uint32_t score, int mission)
{
    if (!records_supported()) return 0;
    int better = !arcade_has || score > arcade_score;
    if (better) arcade_score = score;
    if (mission > arcade_mission) arcade_mission = mission;
    arcade_has = 1;
    save();
    return better && score > 0;
}

int records_rush(int defeated, uint32_t frames, int cleared)
{
    if (!records_supported()) return 0;
    int better = 0;
    if (cleared && (!rush_frames || frames < rush_frames)) { rush_frames = frames; better = 1; }
    if (defeated > rush_defeated) { rush_defeated = defeated; better |= !rush_frames; }
    save();
    return better;
}

uint32_t records_rush_frames(void) { return rush_frames; }
int records_rush_defeated(void) { return rush_defeated; }

uint32_t records_mission_frames(int m) { return m >= 1 && m <= MAX_MISSIONS ? best[m - 1].frames : 0; }
uint32_t records_mission_score(int m) { return m >= 1 && m <= MAX_MISSIONS ? best[m - 1].score : 0; }
int records_mission_has(int m) { return m >= 1 && m <= MAX_MISSIONS && best[m - 1].has; }
uint32_t records_arcade_score(void) { return arcade_has ? arcade_score : 0; }
int records_arcade_mission(void) { return arcade_has ? arcade_mission : 0; }

void records_time_text(uint32_t frames, char *b, int n)
{
    uint64_t cs = (uint64_t)frames * 1000000 / 591856;                 /* hundredths of a second */
    snprintf(b, (size_t)n, "%u:%02u.%02u", (unsigned)(cs / 6000), (unsigned)(cs / 100 % 60), (unsigned)(cs % 100));
}
