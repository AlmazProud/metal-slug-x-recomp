/* Music, effects and voices menu, and gamepad rumble (Phases 6B and 6C).
 *
 * Menu: when it opens, the full game state is saved (savestate.c) and the 68000 stops running;
 * the Z80 and the YM2610 run on their own (ng_sound_only_*). Each sound is played the way the
 * game does when it changes music: 03 03 06 (driver reset), the mode command and the command,
 * one frame apart. On close the state is restored: the game resumes exactly where it was.
 *
 * Tabs: Music, Effects, Voices and Recent (the last sounds the game requested, to identify one:
 * for example, fire the laser and open the menu). The user can rename each sound (F2 or R) and
 * choose its rumble (V, or X on the gamepad): saved to the user's sound file (sonidos.ini).
 *
 * Rumble: the game does not report explosions, but it does send a sound command for each one.
 * The driver mode is tracked and the sound's rumble is requested: the user's choice or, if none,
 * the one from the game profile's table of identified effects (game_profile.rumble). Within a frame the
 * strongest wins, and a stronger one in progress is not cut short.
 */
#include "sounds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "lang.h"
#include "neogeo.h"

/* ------------------------------------------------------------------ user data */
enum { VIB_AUTO = -1, VIB_OFF, VIB_LIGHT, VIB_MEDIUM, VIB_STRONG, VIB_COUNT };
static const char *const VIB_NAME[VIB_COUNT] = {"off", "light", "medium", "strong"};
/* Old (Spanish) names, still accepted when loading. */
static const char *const VIB_NAME_OLD[VIB_COUNT] = {"no", "leve", "media", "fuerte"};

/* The driver's modes and silence commands: the game profile's, or Metal Slug X and 3's. */
#define MAX_MODES 4
static const SoundMode CLASSIC_MODES[3] = {{MODE_MUSIC, SOUND_MUSIC}, {MODE_EFFECTS, SOUND_EFFECTS}, {MODE_VOICES, SOUND_VOICES}};
static const uint8_t CLASSIC_STOP[3] = {0x03, 0x03, 0x06};

static const SoundMode *modes(int *n)
{
    if (game_profile.n_sound_modes) {
        *n = game_profile.n_sound_modes > MAX_MODES ? MAX_MODES : game_profile.n_sound_modes;
        return game_profile.sound_modes;
    }
    *n = 3;
    return CLASSIC_MODES;
}

static const uint8_t *stop_commands(int *n)
{
    if (game_profile.n_sound_stop) { *n = game_profile.n_sound_stop; return game_profile.sound_stop; }
    *n = 3;
    return CLASSIC_STOP;
}

static int mode_index(uint8_t mode)
{
    int n;
    const SoundMode *s = modes(&n);
    for (int i = 0; i < n; i++)
        if (s[i].mode == mode) return i;
    return -1;
}

/* The menu tab of a mode (SOUND_*), -1 if it is not a driver mode. */
static int mode_category(uint8_t mode)
{
    int n, i = mode_index(mode);
    return i < 0 ? -1 : modes(&n)[i].category;
}

static struct {
    char path[1100];
    char *name[MAX_MODES][256];        /* user names (NULL = the profile's) */
    int8_t vib[MAX_MODES][256];          /* user VIB_* (VIB_AUTO = the table's) */
} u;

/* Our own copies: SDL_strdup allocates with the SDL3.dll allocator, and freeing them with free()
   from another C runtime corrupted the heap (hang when renaming the same sound twice). */
static char *copy_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}

static void user_save(void)
{
    FILE *f = fopen(u.path, "w");
    if (!f) return;
    int n;
    const SoundMode *s = modes(&n);
    fprintf(f, "; %s sound names and rumble (written by the sound menu).\n"
               "; Key: the sound driver's mode and command, in hexadecimal.\n\n[names]\n",
            ng_game->title);
    for (int m = 0; m < n; m++)
        for (int c = 0; c < 256; c++)
            if (u.name[m][c]) fprintf(f, "%02X%02X=%s\n", s[m].mode, c, u.name[m][c]);
    fprintf(f, "\n; rumble: off, light, medium or strong\n[rumble]\n");
    for (int m = 0; m < n; m++)
        for (int c = 0; c < 256; c++)
            if (u.vib[m][c] != VIB_AUTO) fprintf(f, "%02X%02X=%s\n", s[m].mode, c, VIB_NAME[u.vib[m][c]]);
    fclose(f);
}

static void user_load(void)
{
    memset(u.vib, VIB_AUTO, sizeof u.vib);
    FILE *f = fopen(u.path, "r");
    if (!f) return;
    char line[256], section[32] = "";
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { snprintf(section, sizeof section, "%s", line); continue; }
        unsigned key;
        int used = 0;
        if (sscanf(line, "%4x=%n", &key, &used) != 1 || !used) continue;
        int m = mode_index((uint8_t)(key >> 8)), c = key & 0xFF;
        const char *v = line + used;
        if (m < 0) continue;
        /* [nombres] and [vibracion]: section names of older (Spanish) files */
        if ((!strcmp(section, "[names]") || !strcmp(section, "[nombres]")) && *v) {
            free(u.name[m][c]);
            u.name[m][c] = copy_str(v);
        } else if (!strcmp(section, "[rumble]") || !strcmp(section, "[vibracion]")) {
            for (int i = 0; i < VIB_COUNT; i++)
                if (!strcmp(v, VIB_NAME[i]) || !strcmp(v, VIB_NAME_OLD[i])) u.vib[m][c] = (int8_t)i;
        }
    }
    fclose(f);
}

static const Sound *profile_sound(uint8_t mode, uint8_t cmd)
{
    for (int i = 0; i < (*game_profile.n_sounds); i++)
        if (game_profile.sounds[i].mode == mode && game_profile.sounds[i].command == cmd) return &game_profile.sounds[i];
    return NULL;
}

/* A built-in (profile) name in the frontend language: "Effect 2A" is translated word by word,
   the rest through the table (lang.c). */
static const char *builtin_name(const char *n)
{
    static const char *const PREFIX[3] = {"Track", "Effect", "Voice"};
    static char b[40];
    for (int i = 0; i < 3; i++) {
        size_t l = strlen(PREFIX[i]);
        if (!strncmp(n, PREFIX[i], l) && n[l] == ' ' && strlen(n + l + 1) == 2) {
            snprintf(b, sizeof b, "%s %s", T(PREFIX[i]), n + l + 1);
            return b;
        }
    }
    return T(n);
}

/* User names are shown verbatim; built-in ones, translated. */
static const char *sound_name(uint8_t mode, uint8_t cmd)
{
    static char b[40];
    int m = mode_index(mode);
    if (m >= 0 && u.name[m][cmd]) return u.name[m][cmd];
    const Sound *s = profile_sound(mode, cmd);
    if (s) return builtin_name(s->name);
    int cat = mode_category(mode);
    snprintf(b, sizeof b, "%s %02X", T(cat == SOUND_MUSIC ? "Track" : cat == SOUND_VOICES ? "Voice" : "Effect"), cmd);
    return b;
}

/* ------------------------------------------------------------------ rumble: table */
typedef struct { uint16_t low, high, ms; } Rumble;
static const Rumble LEVEL[VIB_COUNT] = {{0, 0, 0}, {0x2000, 0x4000, 45}, {0x9000, 0x7000, 150}, {0xE000, 0x9800, 320}};


static const Rumble *rumble_of(uint8_t mode, uint8_t cmd)
{
    int m = mode_index(mode), cat = mode_category(mode);
    if (m < 0 || cat == SOUND_MUSIC) return NULL;
    if (u.vib[m][cmd] != VIB_AUTO) return u.vib[m][cmd] ? &LEVEL[u.vib[m][cmd]] : NULL;
    if (cat != SOUND_EFFECTS) return NULL;
    static Rumble r;                             /* the game profile's table (identified effects) */
    for (int i = 0; i < game_profile.n_rumble; i++)
        if (game_profile.rumble[i].command == cmd) {
            r = (Rumble){game_profile.rumble[i].low, game_profile.rumble[i].high, game_profile.rumble[i].ms};
            return &r;
        }
    return NULL;
}

/* ------------------------------------------------------------------ menu */
enum { CAT_MUSIC, CAT_EFFECTS, CAT_VOICES, CAT_RECENT, CAT_COUNT };
static const char *const CATEGORY[CAT_COUNT] = {"Music", "Effects", "Voices", "Recent"};
#define RECENT_MAX 16

typedef struct { uint8_t mode, cmd; } SoundKey;

static struct {
    SDL_Window *window;
    int is_open, category, sel[CAT_COUNT];
    int playing;                                 /* 1 if something is playing */
    SoundKey current;
    uint8_t *state;
    size_t size;
    uint8_t queue[16];
    int queue_len;
    Uint64 since;                                /* playback start (ns) */
    uint8_t rumble_mode;                         /* driver mode the rumble was tracking */
    SoundKey recent[RECENT_MAX];                 /* newest first */
    int recent_count;
    int editing;
    char text[32];
    Uint64 ignore_until;                         /* the text of the key that started the edit */
} m;

static uint8_t *driver_mode(void);

static void add_recent(uint8_t mode, uint8_t cmd)
{
    int i;
    for (i = 0; i < m.recent_count; i++)
        if (m.recent[i].mode == mode && m.recent[i].cmd == cmd) break;
    if (i == m.recent_count && m.recent_count < RECENT_MAX) m.recent_count++;
    if (i == RECENT_MAX) i = RECENT_MAX - 1;
    memmove(m.recent + 1, m.recent, (size_t)i * sizeof m.recent[0]);
    m.recent[0] = (SoundKey){mode, cmd};
}

/* The tab's list: up to `max` sounds. */
static int list_sounds(int cat, SoundKey *out, int max)
{
    if (cat == CAT_RECENT) {
        memcpy(out, m.recent, (size_t)m.recent_count * sizeof *out);
        return m.recent_count;
    }
    int n = 0;
    for (int i = 0; i < (*game_profile.n_sounds) && n < max; i++)
        if (mode_category(game_profile.sounds[i].mode) == cat) out[n++] = (SoundKey){game_profile.sounds[i].mode, game_profile.sounds[i].command};
    return n;
}

static int selected(SoundKey *c)
{
    SoundKey l[256];
    int n = list_sounds(m.category, l, 256);
    if (!n) return 0;
    if (m.sel[m.category] >= n) m.sel[m.category] = n - 1;
    *c = l[m.sel[m.category]];
    return 1;
}

static void enqueue(const uint8_t *c, int n)
{
    if (n > (int)sizeof m.queue) n = (int)sizeof m.queue;
    memcpy(m.queue, c, (size_t)n);
    m.queue_len = n;
}

/* The silence commands, then the mode and the command, one frame apart. */
static void play(SoundKey c)
{
    int n;
    const uint8_t *stop = stop_commands(&n);
    uint8_t s[sizeof m.queue];
    if (n > (int)sizeof s - 2) n = (int)sizeof s - 2;
    memcpy(s, stop, (size_t)n);
    s[n] = c.mode;
    s[n + 1] = c.cmd;
    enqueue(s, n + 2);
    m.playing = 1;
    m.current = c;
    m.since = SDL_GetTicksNS();
}

static void stop(void)
{
    int n;
    const uint8_t *c = stop_commands(&n);
    enqueue(c, n);
    m.playing = 0;
}

void sounds_init(SDL_Window *window, const char *path)
{
    m.window = window;
    snprintf(u.path, sizeof u.path, "%s", path);
    user_load();
}

void sounds_show(void)
{
    if (m.is_open) return;
    free(m.state);
    m.state = ng_state_save(&m.size);
    if (!m.state) return;
    m.is_open = 1;
    m.playing = 0;
    m.rumble_mode = *driver_mode();
    stop();                                      /* silences whatever the game was playing */
}

void sounds_close(void)
{
    if (!m.is_open) return;
    if (m.editing) { m.editing = 0; if (m.window) SDL_StopTextInput(m.window); }
    m.is_open = 0;
    if (m.state) ng_state_load(m.state, m.size);
    *driver_mode() = m.rumble_mode;
    free(m.state);
    m.state = NULL;
}

int sounds_open(void) { return m.is_open; }

void sounds_frame(void)
{
    if (m.queue_len) {                           /* one command per frame */
        ng_sound_only_command(m.queue[0]);
        memmove(m.queue, m.queue + 1, (size_t)--m.queue_len);
    }
    ng_sound_only_advance(NG_CYCLES_PER_FRAME);
}

static void rename_sound(void)
{
    SoundKey c;
    if (!selected(&c) || !m.window) return;
    snprintf(m.text, sizeof m.text, "%s", sound_name(c.mode, c.cmd));
    m.editing = 1;
    m.ignore_until = SDL_GetTicksNS() + 50000000ULL;
    SDL_StartTextInput(m.window);
}

static void cycle_rumble(void)
{
    SoundKey c;
    int i;
    if (!selected(&c) || (i = mode_index(c.mode)) < 0 || mode_category(c.mode) == SOUND_MUSIC) return;
    int v = u.vib[i][c.cmd] + 1;                 /* auto -> off -> light -> medium -> strong -> auto */
    u.vib[i][c.cmd] = (int8_t)(v >= VIB_COUNT ? VIB_AUTO : v);
    user_save();
}

void sounds_navigate(int t)
{
    SoundKey l[256], c;
    int n = list_sounds(m.category, l, 256);
    int *s = &m.sel[m.category];
    switch (t) {
    case 0: if (n) *s = (*s + n - 1) % n; break;
    case 1: if (n) *s = (*s + 1) % n; break;
    case 2: case 9: m.category = (m.category + CAT_COUNT - 1) % CAT_COUNT; break;
    case 3: case 10: m.category = (m.category + 1) % CAT_COUNT; break;
    case 4: if (selected(&c)) play(c); break;
    case 6: stop(); break;
    case 7: rename_sound(); break;
    case 8: cycle_rumble(); break;
    default: sounds_close(); break;
    }
}

int sounds_editing(void) { return m.editing; }

void sounds_text(const char *s)
{
    if (!m.editing || SDL_GetTicksNS() < m.ignore_until) return;
    size_t n = strlen(m.text);
    for (; *s && n + 1 < sizeof m.text; s++)
        if ((unsigned char)*s >= 0x20 && (unsigned char)*s < 0x7F) m.text[n++] = *s;   /* the font is ASCII */
    m.text[n] = 0;
}

void sounds_edit_key(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_BACKSPACE) {
        size_t n = strlen(m.text);
        if (n) m.text[n - 1] = 0;
        return;
    }
    if (sc != SDL_SCANCODE_RETURN && sc != SDL_SCANCODE_KP_ENTER && sc != SDL_SCANCODE_ESCAPE) return;
    m.editing = 0;
    SDL_StopTextInput(m.window);
    SoundKey c;
    int i;
    if (sc == SDL_SCANCODE_ESCAPE || !selected(&c) || (i = mode_index(c.mode)) < 0) return;
    free(u.name[i][c.cmd]);
    u.name[i][c.cmd] = NULL;
    const Sound *p = profile_sound(c.mode, c.cmd);
    /* the built-in name (in English or as currently shown) is not a user name */
    if (m.text[0] && !(p && (!strcmp(p->name, m.text) || !strcmp(builtin_name(p->name), m.text))))
        u.name[i][c.cmd] = copy_str(m.text);
    user_save();
}

/* ------------------------------------------------------------------ drawing */
void sounds_draw(SDL_Renderer *r, int w, int h)
{
    if (!m.is_open) return;
    float k = font_factor(w, h);
    SDL_SetRenderScale(r, k, k);
    float vw = (float)w / k, vh = (float)h / k, cx = vw / 2;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 215);
    SDL_RenderFillRect(r, NULL);
    char b[128];
    font_centered(r, cx, 12, 3, INK_GOLD, T("MUSIC AND SOUNDS"));

    /* tabs */
    float width = 0;
    for (int c = 0; c < CAT_COUNT; c++) width += font_width(T(CATEGORY[c]), 1.5f) + 24;
    float x = cx - width / 2 + 12;
    for (int c = 0; c < CAT_COUNT; c++) {
        int is_sel = c == m.category;
        const char *tab = T(CATEGORY[c]);
        if (is_sel) {
            SDL_SetRenderDrawColor(r, 255, 190, 60, 50);
            SDL_FRect f = {x - 6, 42, font_width(tab, 1.5f) + 12, 18};
            SDL_RenderFillRect(r, &f);
        }
        font_text(r, x, 45, 1.5f, is_sel ? INK_GOLD : INK_GRAY, tab);
        x += font_width(tab, 1.5f) + 24;
    }

    /* scrolling list: name, key, duration and rumble */
    SoundKey l[256];
    int n = list_sounds(m.category, l, 256), sel = m.sel[m.category];
    if (sel >= n) sel = n ? n - 1 : 0;
    const float L = 13, x0 = cx - 210, y0 = 80;
    int visible = (int)((vh - y0 - 58) / L);
    if (visible < 3) visible = 3;
    int first = sel - visible / 2;
    if (first > n - visible) first = n - visible;
    if (first < 0) first = 0;
    if (!n) font_centered(r, cx, y0 + 20, 1, INK_GRAY,
                            T(m.category == CAT_RECENT ? "Nothing has played in the game yet" : "Empty"));
    int with_rumble = m.category != CAT_MUSIC;
    /* uppercase key (the font is uppercase anyway): a shorter translation than the menu's "Rumble" */
    if (with_rumble && n) font_text(r, x0 + 372, y0 - 14, 1, INK_GRAY, T("RUMBLE"));
    for (int p = first; p < n && p < first + visible; p++) {
        float y = y0 + (float)(p - first) * L;
        SoundKey c = l[p];
        int is_sel = p == sel, sounding = m.playing && c.mode == m.current.mode && c.cmd == m.current.cmd;
        int ink = sounding ? INK_GREEN : is_sel ? INK_GOLD : INK_SILVER;
        if (is_sel) {
            SDL_SetRenderDrawColor(r, 255, 190, 60, 40);
            SDL_FRect f = {x0 - 6, y - 3, 432, L};
            SDL_RenderFillRect(r, &f);
            font_text(r, x0 - 4, y, 1, INK_GOLD, ">");
        }
        if (is_sel && m.editing) {
            snprintf(b, sizeof b, "%s_", m.text);
            font_text(r, x0 + 8, y, 1, INK_GREEN, b);
        } else {
            font_text(r, x0 + 8, y, 1, ink, sound_name(c.mode, c.cmd));
        }
        snprintf(b, sizeof b, "%02X:%02X", c.mode, c.cmd);            /* driver key */
        font_text(r, x0 + 256, y, 1, INK_GRAY, b);
        const Sound *s = profile_sound(c.mode, c.cmd);
        if (s) {
            if (s->tenths == 0xFFFF) snprintf(b, sizeof b, "%s", T("loop"));
            else snprintf(b, sizeof b, "%d.%ds", s->tenths / 10, s->tenths % 10);
            font_text(r, x0 + 356 - font_width(b, 1), y, 1, ink, b);
        }
        if (with_rumble && mode_category(c.mode) != SOUND_MUSIC) {
            int i = mode_index(c.mode), v = i >= 0 ? u.vib[i][c.cmd] : VIB_AUTO;
            const char *t = v != VIB_AUTO ? T(VIB_NAME[v]) : rumble_of(c.mode, c.cmd) ? "auto" : "-";
            font_text(r, x0 + 372, y, 1, v != VIB_AUTO ? INK_GOLD : INK_GRAY, t);
        }
    }

    /* status and help */
    float y = vh - 46;
    if (m.playing) {
        double secs = (double)(SDL_GetTicksNS() - m.since) / 1e9;
        snprintf(b, sizeof b, T("Playing: %s  %02d:%02d"), sound_name(m.current.mode, m.current.cmd), (int)secs / 60, (int)secs % 60);
        font_centered(r, cx, y, 1, INK_GREEN, b);
    }
    if (m.editing) {
        font_centered(r, cx, y + 16, 1, INK_SILVER, T("Type the name   Enter save   Esc cancel"));
    } else {
        font_centered(r, cx, y + 16, 1, INK_GRAY, T("Enter/A listen   Del/Y stop   F2 rename"));
        font_centered(r, cx, y + 30, 1, INK_GRAY, T("V/X rumble   </> LB/RB tab   Esc/B back"));
    }
    SDL_SetRenderScale(r, 1, 1);
}

/* ------------------------------------------------------------------ rumble */
static struct {
    SDL_Gamepad **gamepads;
    int gamepad_count, strength;
    uint8_t mode;
    const Rumble *requested;
    Uint64 end;                                  /* end of the rumble in progress (ns) */
    uint32_t force;                              /* of the rumble in progress */
} v = {NULL, 0, 70, 0xFF, NULL, 0, 0};

static uint8_t *driver_mode(void) { return &v.mode; }

/* The game's commands: a mode command selects the driver mode, the first silence command ends it.
   With the profile's own modes, any other command below 0x20 is a control command that ends it
   too (Metal Slug 5: every sound is a mode and a command). */
static void on_command(uint8_t c)
{
    int n;
    if (mode_index(c) >= 0) { v.mode = c; return; }
    if (c == stop_commands(&n)[0] || (game_profile.n_sound_modes && c < 0x20)) { v.mode = 0xFF; return; }
    if (m.is_open || v.mode == 0xFF || c < 0x20) return;
    add_recent(v.mode, c);
    const Rumble *p = rumble_of(v.mode, c);
    if (p && (!v.requested || p->low > v.requested->low)) v.requested = p;
}

void rumble_init(SDL_Gamepad **gamepads, int n)
{
    v.gamepads = gamepads;
    v.gamepad_count = n;
    ng_on_sound_command = on_command;
}

void rumble_strength(int strength) { v.strength = strength; }

void rumble_frame(void)
{
    if (!v.requested) return;
    const Rumble *p = v.requested;
    v.requested = NULL;
    if (v.strength <= 0 || m.is_open) return;
    Uint64 now = SDL_GetTicksNS();
    if (now < v.end && p->low < v.force) return;           /* do not cut a stronger one short */
    Uint16 low = (Uint16)(p->low * (uint32_t)v.strength / 100);
    Uint16 high = (Uint16)(p->high * (uint32_t)v.strength / 100);
    for (int j = 0; j < v.gamepad_count; j++)
        if (v.gamepads[j]) SDL_RumbleGamepad(v.gamepads[j], low, high, p->ms);
    v.end = now + (Uint64)p->ms * 1000000ULL;
    v.force = p->low;
}
