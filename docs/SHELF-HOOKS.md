# PS1 shelf v3: hooks into upstream files

The shelf lives in our own files. Upstream files only get one-line calls, so merges from
`upstream` (darkxex/SwanStationPS5) stay easy.

## Our files
| File | What |
|---|---|
| `src/ui/ps1_kit.c/.h` | Drawing kit in the shell's 640x480-widened-to-16:9 units. Background fan, spheres, plates, plaques, info bar, arrow. |
| `src/ui/ps1_bios.c` | Reads the ROM font from the user's own BIOS file at runtime. Uses a fallback face without one. |
| `src/ui/ps1_record.c/.h` | Per-game record `<states>/<id>.shelf`: play time, last played, favourite, boot disc. |
| `src/ui/ps1_shelf.inc` | Main menu, library (9x3 / 6x2; 7x3 / 5x2 in 4:3), v7 game page, full-text search keypad, cover picker (Triangle on the game page), fades. Included by `coverflow.c`. |
| `src/ui/ps1_pslogo.c` | The PS1 opening, drawn and played from the user's BIOS file: the Sony screen and the PlayStation screen (logo model from `<data>/cache/pslogo.tmd`), their notes from the BIOS's VAB bank, the real power-on timing. |
| `src/ui/ps1_chd.c` | Sectors from a .chd (libchdr) for the optional logo read. |
| `src/ui/ps1_settings.inc` | Settings in the CD Player style; help wraps to 2-3 lines or scrolls; per game Square = row back to global, RESET TO GLOBAL plate. Included by `settings_screen.c`. |
| `src/ui/ps1_menu.inc` | In-game menu: the game in the box, 20 slots as the CD grid (1-20), plates. Included by `menu_screen.c`. |
| `src/ui/ps1_memcards.inc` | Memory card manager: card rows, EXIT/BACK UP/RESTORE/DELETE plates, the 3x5 block lattice. Included by `memcards_screen.c`. |
| `assets/fonts/Asap-Black.ttf` | Lettering face (OFL). |
| `tools/ui-preview/ps1-v3.txt` | Preview script that shows every screen once. |
| `tools/ui-preview/ps1-v3-pass2*.txt` | Second pass: keypad, fades, picker, per-game reset, help, memory cards; `-shapes` run with 16:9 and 4:3. |

No Sony assets in the repo. The boot logo and chime come from the user's BIOS: the existing startup animation
runs the real BIOS with no disc. The ROM font is read from the BIOS at runtime. Console sounds stay in
`/data/SwanStationPS5/console-sounds/`, outside git.

## Hooks (all marked `shelf v3` in the source)
| File | Hook |
|---|---|
| `src/main.c` | `#include "ui/ps1_kit.h"`, `"ui/ps1_record.h"` |
| `src/main.c` `main` | PS1 theme: `ps1_opening_begin()` instead of the BIOS startup (our own opening, no emulation); `ps1k_intro_played()` then and after a game (no second startup.wav). `opening_region()`. |
| `src/main.c` `main` | `ps1_opening_preload()` right after the settings load; `repair_usb_folder()` after an import; no version question in the fork (`SwanStationPS5_NO_SELF_UPDATE`); `startup: main loop, first frame` in the log. |
| `src/main.c` `nav_pressed` | `ps1k_nav_fresh(pressed);` (real presses, so a held direction at an edge stays silent) |
| `src/main.c` `app_start_game` | `ps1_record_sync(g);` after `last_played` is set |
| `src/main.c` `app_stop_game` | `ps1_record_sync(app.game);` after `stats_save()` |
| `src/main.c` `main` | `ps1_shelf_after_game();` when the app restarts after a game (opens that game's page) |
| `src/main.c` `main` | `plat_want_output(-1)`: the display's native mode instead of a fixed 4K |
| `src/platform/plat_sdl.c` | `output_wanted < 0` -> `vkp_want_size(-1, -1)`. On PS5, `plat_ui_scale()` = output height / 1080 with the GPU UI. |
| `src/platform/vk/vk_present.c/.h` | `want_width < 0` picks the display's physical mode (else its biggest up to 4K). Adds `vkp_output_size()`. |
| `src/ui/coverflow.c` | Forward declarations of the search/picker functions. `build_view` and `shelf_show_game` use `ps1_search_match()` in TH_PS1. `picker_draw` -> `ps1_picker_draw()`, `filter_input` -> `ps1_search_input()`, `filter_draw` -> `ps1_search_draw()`. |
| `src/ui/coverflow.c` | `ps1_dialog_opened()` opens the game page. `circle_on_shelf` adds `ps1_quit_allowed()`. `ps1_after_start()` after `app_start_game`. `ps1_cover_window()` then `covers_update_view(..., ps1_cover_centre())`. |
| `src/ui/settings_screen.c` | `ps1_settings_backdrop()` instead of `ps1_tiles()`. `LIBRARY_LAYOUTS` and a "Library layout" row in the Library tab. `SCREEN_SHAPES` / `SPLATTERS` and their rows. `pressed = ps1_settings_input(pressed)` before the row handling. About tab: Version row (new help), "Created by" replaced by a Credits group (port, PlayStation UI, emulator, renderer, PSXS5) with `SP_CREDIT_*` values. |
| `src/main.c` (BIOS) | `app_bios_dirs()` / `app_bios_present()` / `app_bios_recheck()` / `app_bios_set_folder()`: the BIOS folder setting (default /data/SwanStationPS5/bios), then the legacy folders; `bios_folder` peeked from shelf.ini before the BIOS choices are scanned. |
| `src/config.c`, `SwanStationPS5.h` | `bios_folder` setting (shelf.ini), `SwanStationPS5_BIOS_FOLDER`. |
| `src/covers.c/.h` | `covers_set_window()` (load radius + LRU cap; -1 = stock radii), `covers_age()` for the fade in; nearest-first worker and uploads, out-of-window requests dropped; preview-only `SwanStationPS5_COVER_DELAY_MS`. |
| `src/ui/sfx.c/.h` | On upstream's voice mixer (1.6.5): the console sounds (`sfx_load_console`), the extended events, `sfx_play_pcm()` (a fixed-gain voice for the opening), `sfx_stop_all()`. |
| `src/ui/memcards_screen.c` | `#include "ps1_memcards.inc"` in place of the old PS1 screen. |
| `src/ui/ps1.c/.h` | `ps1_splat_smooth()`: smooth splatters generated at the display's resolution. |
| `src/SwanStationPS5.h` | `int shelf_layout, shelf_shape, shelf_splat;` in `Settings` |
| `src/config.c` | Loads and saves `shelf_layout`, `shelf_shape`, `shelf_splat`, `shelf_logo_scan`. The settings file is `shelf.ini` (`config_paths`, `config_user_paths`); `config_load` imports `SwanStationPS5.ini` once (`config_imported()`); the official app's file is never written. |
| `src/util.c` | Log lines carry seconds since start (`SwanStationPS5_uptime()`). | `state_slot % 20` (20 save slots). |
| `src/ui/text.c/.h` | `FONT_PS1` face = `fonts/Asap-Black.ttf` |
| `src/ui/menu_screen.c` | `MENU_SLOTS 20` for the slot thumbnails and notes. The PS1 game menu (`ps1_menu.inc`) shows the 20 slots ten at a time, numbered 1-20. The save / load toasts add `TH_PS1` so they say 1-20 too. |

The older PS1-theme branches (ps1-console-shelf, ps1-shelf-v2) also left `TH_PS1` branches in upstream files.
`git diff upstream/main --stat -- src` lists all of them.

## Upstream merge (one command)
```sh
git fetch upstream && git checkout shelf-v3 && git merge --no-edit upstream/main
```
On a conflict, keep upstream's side and add back the hook lines from the table above.
`git grep -n "shelf v3" -- src` finds every hook. Then check with the laptop preview:
`bash tools/ui-preview/laptop/preview.sh local --script tools/ui-preview/ps1-v3.txt`.

## Merge log
- 2026-11-16, upstream 1.6.3-1.6.5 (19 commits, new core base): 4 conflicts (`sce_sys/param.json`, `src/SwanStationPS5.h`,
  `src/ui/text.c`, `src/ui/sfx.c`); the preview stubs needed upstream's new RetroAchievements entry points. The core
  submodule is reset to pristine by reversing the previous `tools/patches/SSPS5.patch` before the build applies the new one.
