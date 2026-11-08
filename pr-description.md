Adds a per-keyer **Load On Scene** option: a list of program scenes the keyer output is shown on. When a listed scene goes live in Program, the keyer loads automatically. On any other program scene, the keyer output is off. In Studio Mode this follows the Program scene, so the keyer comes on when a listed scene is transitioned to Program.

It's the inverse of the existing **Exclude Scene** option, for when a keyer should only appear on a few scenes, without having to exclude all the others. The goal is to automate keyer toggling so a busy OBS operator doesn't have to remember to turn keyers on and off by hand when switching scenes.

| Option | Keyer output is shown on… |
|---|---|
| Exclude Scene | every program scene **except** the listed ones |
| Load On Scene | **only** the listed program scenes |

## Usage
Gear (config) button → **Load On Scene** → check the program scenes the keyer should be shown on. Click a checked scene again to remove it.

## Behaviour
- **An empty list turns the feature off.** Existing setups behave exactly as before.
- **Auto-load.** When a listed scene goes live and the keyer has nothing selected (cleared, or **Hide After** ran), it selects and shows its last used keyer scene. If none has been used since OBS started, it uses the first in its list.
- **The keyer's selection is kept off-list.** Off-list scenes only clear the output, so it comes back on the next listed scene.
- **Hide After still applies.** The keyer shows for that long each time a listed scene goes live.
- **Manual clicks win.** Selecting a keyer scene in the dock shows it immediately, whatever is in Program. Load On Scene only acts when the program scene changes.
- **Exclude Scene takes precedence** if a scene is in both lists.
- **Changes apply immediately.** Adding or removing a scene re-evaluates the current program scene.
- **Each keyer has its own list**, including keyers on extra views/canvases. For those, the active scene of that view/canvas is used, looking through an in-progress transition.
- **Saved per keyer** as `load_scenes: [{ "name": "<scene>" }, …]`.

## obs-websocket
Two new vendor requests on the `downstream-keyer` vendor, matching `dsk_add_exclude_scene` / `dsk_remove_exclude_scene`:

| Request | Fields |
|---|---|
| `dsk_add_load_scene` | `dsk_name` (required), `scene` (required), `view_name` (optional) |
| `dsk_remove_load_scene` | `dsk_name` (required), `scene` (required), `view_name` (optional) |

The response has `success`, plus `error` when `view_name` is unknown or a required field is missing.

## Changes
- `downstream-keyer.cpp/.hpp`: `load_scenes` set, save/load, the check and auto-load in `SceneChanged`, last-used keyer scene tracking, and add/remove/query helpers
- `downstream-keyer-dock.cpp/.hpp`: the **Load On Scene** config submenu and the two websocket requests
- `data/locale/en-US.ini`: `LoadOnScene` string

## Testing
**Tested on Windows 11 x64**, building against the OBS 31.1.0 deps with MSVC 2022 (RelWithDebInfo, no warnings):
- Installed the build into OBS and used the **Load On Scene** menu to assign a keyer to one scene.
- With Studio Mode on and nothing selected in the keyer, the keyer loaded automatically when that scene was taken to Program, was not present on any other scene, and loaded again on returning to the scene.
- Code is clang-format clean against the OBS style with `ColumnLimit: 132`.

**Not yet tested:**
- **macOS and Linux.** I haven't built or run it on these; I'm relying on this PR's CI builds.
- **Other parts of the feature.** The obs-websocket requests, Exclude Scene and Load On Scene set on the same scene, and keyers on extra views/canvases haven't been exercised yet.

## Sign-off

Signed-off-by: Micah Harman \<micahharman20@gmail.com\>

Assisted-by: Claude:claude-opus-5-5