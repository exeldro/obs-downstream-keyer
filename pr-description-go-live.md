Adds a "set up, then take" workflow to each downstream keyer. Keyer scenes get check boxes. Checked scenes go on air with a new **Go Live** button, can be checked in the Studio Mode preview with **Go to Preview**, or can go live or off together with the program scene transition. Several scenes can be live on one keyer at once.

The goal is the same as Load On Scene: let a busy operator set up the next graphic without it going on air by accident, and then take it at the right moment, ideally together with the scene change.

**This builds on #67 (Load On Scene).** The first seven commits are that PR's. Once #67 is merged, this PR only contains the Go Live commits.

## Usage
Each keyer tab now has a column of controls to the right of its scene list:

- **Keyer Action:** **Go Live**, **Go to Preview** (Studio Mode only), and **Keyer Off**, which replaces the pause (None) button.
- **Keyer Behavior on Transition:** **Go Live at Scene Change** and **Keyer Off at Scene Change**, which replace the Tie check box. At most one is on, and with neither on the keyer is left unchanged.
- **Include Audio** in the gear menu.

## Behaviour
- **Selecting no longer goes on air.** The check box on the right of each scene name arms it. Clicking the name only highlights it for move and remove.
- **Several scenes at once.** Checked scenes go live together, layered in list order with the top of the list on top. More than one scene is put on the output as a private scene holding them as layers, so the keyer's transition blends the group as one picture.
- **Tally colours.** Live scenes are shown in red bold and previewed scenes in green bold. **Go Live** turns red when the checked scenes differ from what is live.
- **Go Live** uses the keyer's transitions. If a preview is showing, the previewed scenes go live and the preview is cleared.
- **Keyer Off** only clears the output. The check boxes and any preview are kept.
- **Go to Preview.** A plugin can't draw over the Studio Mode preview, so the checked scenes are added to the preview scene as a temporary, locked scene item at the top, tagged in its private settings.
  - The preview follows the check boxes and the selected preview scene.
  - On a program transition to that scene, the keyer scenes ride in with it. When the transition ends, the same source is cut onto the keyer output and the item is removed, so nothing visibly changes. This handles **Duplicate Scene** (the item is copied into the private program copy and removed there too) and **Swap Scenes**.
  - The item is never added to the scene that is currently on Program.
  - Leftover items are purged on `FINISHED_LOADING` and `SCENE_COLLECTION_CHANGED`, for example after a crash.
- **Keyer Behavior on Transition.** On the main output this acts when the program transition **starts**, via the global `source_transition_start` signal filtered to output channel 0, so the keyer and scene transitions run together. The later `SCENE_CHANGED` for that scene is skipped. Views and canvases still act on their scene change, as before.
- **Include Audio, off by default.** Without it, the keyer output is wrapped in a small private video-only source (`downstream_keyer_video_only`, hidden from the source list), so the keyer scenes' audio isn't mixed into the output. With it on, the audio is mixed as before. Sources still show in the Audio Mixer, because their meters show their own level.
- **Hide After** starts when scenes actually go live, never during a preview.
- **Exclude Scene and Load On Scene still take priority**, including over a preview take.

## Compatibility
- **Old settings load:** the scene that was on air stays live, nothing is checked, and `tie` becomes **Go Live at Scene Change**.
- **New keys:** `checked` on each scene, `live_scenes`, `scene_change_mode` and `include_audio`. `scene` and `tie` are still written.
- **Behaviour change:** Include Audio defaults to off, so keyers that played audio before are silent until it is checked.
- **Existing websocket requests keep working:**
  - `dsk_select_scene` checks only that scene and puts it live.
  - `dsk_get_scene` returns the first live scene.
  - `dsk_set_tie` sets **Go Live at Scene Change**.
  - `dsk_scene_changed` reports the live scenes, separated by commas.
- **Existing hotkeys:**
  - The per-scene **Show on** / **Hide on** hotkeys add the scene to, or remove it from, the live scenes right away.
  - The tie hotkeys toggle **Go Live at Scene Change**.

## obs-websocket
Two new vendor requests on the `downstream-keyer` vendor:

| Request | Fields |
|---|---|
| `dsk_go_live` | `dsk_name` (required), `scene` (optional: check only this scene first), `view_name` (optional) |
| `dsk_go_to_preview` | `dsk_name` (required), `scene` (optional), `view_name` (optional) |

The response has `success`, plus `error` when `view_name` is unknown or `dsk_name` is missing.

## Changes
- `downstream-keyer.cpp/.hpp`: the side column, the right-hand check box delegate, live and preview state, building the layered source, Go Live, Go to Preview with take and cleanup, scene change modes, the video-only wrapper source, save/load, and the new hotkeys
- `downstream-keyer-dock.cpp/.hpp`: the transition start hook, preview and studio mode events, the **Include Audio** menu item, registering the video-only source, the leftover preview purge, and the two websocket requests
- `data/locale/en-US.ini`: new strings

## Testing
**Tested on Windows 11 x64 with OBS 33.0**, building against the OBS 31.1.0 deps with MSVC 2022 (RelWithDebInfo, no warnings):
- Installed the build into OBS and checked the side column layout and button labels in the dock with the default theme.
- With **Include Audio** off, put a keyer scene with audio on air and recorded. The recording was silent.
- Code is clang-format clean against the OBS style with `ColumnLimit: 132`.

**Not yet tested:**
- **macOS and Linux.** I haven't built or run it on these; I'm relying on this PR's CI builds.
- **Not systematically tested on Windows yet:**
  - the obs-websocket requests and hotkeys
  - keyers on extra views/canvases
  - Go to Preview with every combination of Duplicate Scene and Swap Scenes
  - transitions with a fixed duration (stingers)

## Sign-off

Signed-off-by: Micah Harman \<micahharman20@gmail.com\>

Assisted-by: Claude:claude-opus-5-5
