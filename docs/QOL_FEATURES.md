# Quality-of-life features (after Phase 5)

Asked for by the user. All of them live in the frontend (`src/port`): the emulated game is not
changed, so every run against MAME still passes.

| Feature | Where | How it works |
|---|---|---|
| Pause when inactive | Settings > Game | The window losing focus (Alt+Tab, the Windows key) stops the emulation and the audio until it comes back (`focus_pause`, on by default) |
| Fast forward | Tab, or the gamepad's right trigger | Held: 4 frames per refresh, silent; not in Arcade mode |
| High resolutions | Settings > Video | "Integer scale": the image at a whole multiple of 224 lines (sharp on 4K / 8K); the sharp filter's intermediate texture goes up to 24x (8K); "Window size" up to the largest that fits the monitor (16x at most) |
| Continue | Start screen | A game in progress (a player playing or in the continue countdown, the mission known; not Arcade mode or Boss Rush) is autosaved to `resume.bin` every minute of play, when a mission is unlocked, on "Back to start screen" and on quit; the start screen then offers "Continue". Before, only quitting during the game saved it: going back to the start screen first left an old one |
| Arcade mode | Start screen | A whole game without saving or loading, cheats, debug options, slowdown changes or fast-forward. Continues chosen before starting (0, 1, 3, 5 or infinite): the first coin is the frontend's, each coin after it is a continue; without continues left the coin slot is closed. At the game over (BIOS player status 3: 1 playing, 2 continue countdown) a page shows player 1's score (`score_p1`, 8 BCD digits at `0x10ED18`), the time played and the continues used |
| Mission mode | Start screen | Missions 1-5; each one unlocks when the previous one is completed in any mode (the game's current mission, `mission_now` = `0x10040C`, goes from n to n + 1 while a player plays: the next-mission step `0xED70C`). Progress in `progress.ini`. A new game starts at the chosen mission through the start mission mechanism verified in Phase 4 (`cheats_start_mission_once`) and then goes on to the next missions |
| Fast boot | Settings > Game | On power-on and reset the BIOS boot (its green screen) runs unseen, silent and with no inputs, 120 frames per refresh, up to the game's first image (`boot_frames` in the profile: Metal Slug 3 frame 665, after the boot screen twice with a watchdog reset between them; Metal Slug X frame 304). The same frames as always, only not shown: the machine's state afterwards is the same as MAME's. A loaded state or "Continue" ends it. On by default (`fast_boot`) |
| Records | Extras > Records; Mission mode; the Arcade card and results | `records.ini` (`src/port/records.c`): per mission the best time and the best score made in it, from the start of the game (or the previous mission's next-mission step, or Mission mode's jump: a step sooner than 3600 frames is that jump) to its own next-mission step; Arcade mode's best score and furthest mission. Only clean runs: no debug dips or developer mode, the original slowdown, no state loaded and no cheat used during the mission. Mission 5 too: its step goes to 5 when the final boss falls (`docs/BOSS_RUSH.md`). Metal Slug 3 only (Metal Slug X's profile has no mission or score address). Test `mslug3_records_missions`: the user's recording, Mission 1 in 14 481 frames (4:04.67, 128 430 points) and Mission 2 in 43 509 (12:15.12, 307 034) |
| Boss health bar | Settings > Game | The life of the bosses from their first hit, an upright bar at the right edge with the boss's name written down its side, drawn over the game from RAM (the game is not changed). `docs/BOSS_RUSH.md`, stage 2 |
| Boss Rush | Game modes (unlocked by completing the game in Arcade or Mission mode) | The seven bosses in a row in one game with Arcade mode's rules and one credit (the Slowdown option is kept; with it off the run is not recorded); the game itself carries score, lives and weapon from one boss to the next; 2 s into the first boss the Heavy Machine Gun's item appears in front of the player (the game's own item: picking it up is their choice; `rush_weapon` in the profile); summary and records (best time, most bosses). `docs/BOSS_RUSH.md`, stage 3 |

## Remastered menu

The menus were redesigned ("remaster" style; mock-ups made with Claude Design and approved by the
user) and reorganized:

| Page | Options |
|---|---|
| Start | Continue · Play · Game modes · Load game · Settings · Extras · Quit |
| Pause | Continue · Saves (save and load) · Settings · Extras · Back to start screen · Quit |
| Game modes | Cards: Arcade (up / down: continues), Mission, Boss Rush (locked until the game is completed in Arcade or Mission mode) |
| Settings | Tabs (LB / RB, Q / E): Video · Audio · Game · Keyboard · Gamepad · System (language, start screen, fast boot, pause when inactive) |
| Extras | Music and sounds · Cheats · Debug (with the F3 panel) · Service menu · Reset |

- **Look:** gunmetal ground, amber accent, warning stripes, a key-hint bar that follows the last
  device used (keyboard or gamepad). Fonts: Black Ops One (titles) and Chakra Petch (text), OFL,
  rasterized with stb_truetype at the window's real pixel size (`src/port/ui_font.c`): sharp up to
  8K. The sound menu and the notices use them too (`font.c` draws with them). Spanish texts now
  carry their accents.
- **Layout:** a canvas 720 units tall, at least 960 wide, so it adapts from 4:3 windows to 16:9
  full screen.
- **Continue panel:** every autosave also writes `resume.png` (the screen) and `resume.ini`
  (mission, score, date); the start screen shows them next to "Continue".
- **Pause:** the current mission and player 1's score, when the profile has them (Metal Slug 3).
- **Mouse:** moving over an option selects it; the left button does what Enter does (on the value
  arrows it changes the value), the right button goes back, the wheel moves the selection (on the
  cards, to the side). Tabs, cards and the key hints can be clicked. In the sound menu: the wheel
  and the right button. During the game the cursor hides after 2 s without moving. Window check:
  `PD_WINDOW_TEST=40,file.png,mouse` (a click on Game modes, then on the Arcade card's up arrow).

Other changes:

- **"Play" after a reset** waits until the game is up (900 frames from power-on): a coin during
  the boot was ignored.
- **Tests:** `mslug3_progress_unlocks` (the user's recording unlocks Mission 2 at frame 15407 and
  Mission 3 at 58916, when the game moves on). Window checks (`PD_WINDOW_TEST`): `arcade`,
  `missions`, `results`, `video`, `resume`, `missionN` and `arcaderun` (Arcade mode with no
  continues and no inputs, up to its results page: game over after 15 minutes, score 0).

## Controls (Settings > Keyboard / Gamepad)

Asked for by the user: controls "like modern games", every one changeable at any time without
going through the whole list, with a picture of the device.

- **Shortcuts are settings** (`config.h` `HK_*`, `[shortcuts]` / `[shortcuts_gamepad]` in the
  ini): menu, pause, quick save / load, previous / next slot (new: F7 / F8), fast forward,
  screenshot, fullscreen, debug panel, service button, developer menu. Defaults: the keys the port
  always had; on the gamepad Guide (menu), the right trigger (fast forward) and Share
  (screenshot), the rest unbound (the user's choice: quick save / load too). Back + Start always
  opens the menu and Alt+Enter is always fullscreen. The triggers count as buttons (`PAD_LT`,
  `PAD_RT`) for the game's actions and the shortcuts.
- **Two tabs**, each a list: the player (1 / 2), "Game controls" (the eleven actions) and
  "Shortcuts" (shared by both players), and "Restore defaults" (that tab, both players). A long
  list scrolls. Enter (A, or a click) waits for the new key or button for that row only; Del / Y
  clears it; Esc cancels (on the menu's own row it is the key), and a gamepad wait ends after 6 s.
  A key already used (by any action of either player, one keyboard, or a shortcut) or a button
  used by the same player or a shortcut swaps with the row's old one, with a notice; the menu's
  key is never left unbound.
- **Names:** keys by SDL's names; gamepad buttons as the connected gamepad prints them (Xbox:
  A / B / X / Y, LB, LT...; PlayStation: Cross / Circle / Square / Triangle, L1, L2...; Nintendo:
  L, ZL, + / -...).
- **The picture** on the right, drawn with shapes (no images): a full keyboard (without the
  number pad) or a gamepad; keys and buttons bound for the tab's player in amber, shortcuts in
  cyan, the other player's keys in faint amber; on the gamepad each bound button has a callout
  with its action. The selected row's key or button blinks, and its name and binding are shown
  under the picture with a legend of the colors.
- **Gamepads, after the user's arcade stick** (a Mayflash F300 in XInput mode, whose lever is the
  left stick, could not be bound):
  - the eight stick directions (left and right stick) are inputs too (`PAD_LS_UP`...
    `PAD_RS_RIGHT`, `leftstick_up`... in the ini), bound like buttons, and the left stick moves
    through the menus as the D-pad does;
  - "Gamepad" (second row of the Gamepad tab): which connected gamepad each player uses, cycled
    with left / right; taking the other player's swaps them. Remembered by the gamepad's SDL GUID
    (`device=` in `[gamepad1]` / `[gamepad2]`): when it connects it takes that player's slot back.
    Before, the first gamepad connected was always player 1;
  - "Gamepad type" (third row, per player, `style=` in the ini): Automatic (as SDL detects it),
    Xbox / XInput, PlayStation, Nintendo or Arcade stick: the buttons' names and the picture. The
    arcade stick picture is a lever (the D-pad or the left stick, as the stick's switch makes it)
    and eight buttons in two rows as XInput sticks map them (Mayflash F300: X Y RB LB over
    A B RT LT), the small buttons on top.
  - "Profile" (third row): four gamepad profiles, each a mapping and a type
    (`[gamepad_profile1]`... in the ini, `profile=` in `[gamepad1]` / `[gamepad2]`). Each player
    uses one; the list edits it (`button[]` / `pad_style[]` are the working copy, stored back on
    every change). The profile chosen with a gamepad connected is linked to that gamepad's GUID
    and loads by itself when it connects (one Xbox pad, one arcade stick). Older ini files: each
    player's mapping becomes profiles 1 and 2.
  - "Restore defaults" moved up (fifth row) and asks for a second press within 3 s; on the
    Gamepad tab it resets this player's profile and the gamepad shortcuts.
  - "Gamepad hides the cursor" (Settings > System, on by default, `gamepad_hides_cursor`): a
    gamepad button or stick hides the mouse cursor and the mouse stops moving the menus'
    selection, until the mouse really moves (4 pixels or more) or a key is pressed.
  - On the gamepad picture the callouts go top to bottom by their part's height (fewer crossing
    lines); bound stick directions show as arrows around the sticks.
- **Window checks** (`PD_WINDOW_TEST=30,file.png,<name>`): `keyboard`, `gamepad`, `padstick` (the
  left stick pushed up while "Up" waits: it gets `leftstick_up`), `padprofile` (profile 2,
  linked to the connected gamepad), `padcapture`, `kbcapture`
  (a row waiting for its key), `kbscroll` (the list scrolled to the shortcuts), `kbswap` ("Down"
  gets Z, the fire key: they swap, as the ini then says), `hotkeys` (next slot and quick save
  through their shortcuts).

## Run-ahead (Settings > Game)

Asked for by the user ("mejorar el input"): less input lag. Off by default (the user's choice);
allowed in every mode, Arcade and Boss Rush too (it does not change the game).

- **The game's own lag**, measured first (`analysis\runahead\measure_lag.py`: a scripted start
  into Mission 1, a button pressed at each of 12 consecutive frames, the first image that differs
  from a run without it): Metal Slug 3 shows the reaction 1 or 2 frames after the press,
  alternating with the phase of its 30 Hz logic, the same for jump, fire and moving.
- **How** (`src/port/runahead.c`): after the real frames of a refresh (and after their audio is
  queued: loading a state drops pending audio), the state is saved in memory, 1 or 2 frames are
  run with the same inputs, their last image is kept, the state is loaded back and that image is
  shown. The frames ahead have no audio and none of the frontend's hooks (rumble, Boss Rush, the
  tests' fingerprints and timed inputs); records, progress and the health bar only see the real
  frames. Not with fast-forward.
- **1 frame** always shows the reaction a frame (16.9 ms) sooner: 0 or 1 frames instead of 1 or 2.
  **2 frames** shows it at once on half of the presses but goes past the game's lag on the other
  half, where the image may jump; the option's help says so.
- **The game does not change:** `mslug3_start_runahead_vs_mame` and `..._audio_vs_mame` run the
  start run against MAME with `PD_RUNAHEAD=2` (run-ahead after every frame in headless mode):
  RAM, palette, image and audio identical. (A first version failed there: the tests' timed inputs,
  applied through `ng_before_input_read`, were consumed by the frames ahead.)
- **Cost** (the start run, 9001 frames, headless): with the recompiled program 1262 frames/s, 614
  with 1 frame ahead, 420 with 2; interpreter only (the release) 474, 241 and 160, still 2.7 times
  real time with 2.
- Window check: `PD_WINDOW_TEST=90,file.png,gametab` (Settings > Game, the Run-ahead row).

## CRT effect (Settings > Video)

Asked for by the user ("shaders"). Done without shaders, with the plain SDL renderer (the user's
choice of the two paths offered: it works on any PC, needs no new tools, keeps the repository
text-only and leaves the Direct3D 11 latency setting alone). `src/port/crt.c`.

- **Styles** (`crt=` in the ini): Off, Arcade (aperture grille: vertical RGB stripes, strong
  lines), TV (slot mask: RGB triads cut by staggered gaps, softer lines, more curvature and
  vignette), Soft (lines and glow only). With it on, Filter and Scanlines are hidden: the effect
  starts from the sharp image.
- **How**, at the output's real pixels, in a texture the size of the image:
  - the sharp image;
  - the lines: one column with a row per output pixel, a gaussian beam profile around the centre
    of each game line, multiplied over it (lighter in small windows, where thin lines would beat
    against the pixels);
  - the mask, tiled and multiplied (only from 2 output pixels per line);
  - part of the image added back (the brightness the lines and the mask take away);
  - the glow: the image shrunk to a half and a quarter (each step blurs it) and added over it, so
    bright areas bloom;
  - a vignette;
  - then onto the window flat, or over a 32 x 24 grid bent like a tube's face ("Curvature", on
    by default, `crt_curve`; the middle of each edge stays on the rectangle, the corners come in;
    Arcade 0.025, TV 0.05, Soft 0.03).
- Window checks: `crt=arcade|tv|soft` in a test ini and `PD_WINDOW_TEST=40,file.png` with a state
  loaded (`PD_LOAD_STATE`).

## Planned (asked about, not started)

Each one only when the user approves it.

- Low resolutions for CRTs, Discord Rich Presence (needs a
  Discord Application ID from the user), rewind.
- Boss Rush with the other bosses (an idea for later): Hi-Do and the Clone Incubator start their
  sections (easy); Hairbuster Riberts and Rugname end long sections; the Twin Ohumein-Conga is not
  found yet (`docs/BOSS_RUSH.md`, stage 4).
