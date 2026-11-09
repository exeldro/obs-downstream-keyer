# Go Live, Go to Preview and Keyer Behavior on Transition

These changes turn each downstream keyer into a "set up, then take" control. Selecting keyer scenes no longer puts them on air. You check the scenes you want, then put them live with **Go Live**, preview them in Studio Mode with **Go to Preview**, or let a program scene transition take them live or off.

## The dock

Each keyer tab has its scene list on the left and a column of controls on the right:

```
[DSK 1][DSK 2]                                       [⚙]
┌──────────────────────────────┐ ┌──────────────────────────────┐
│ Lower Third - Pastor     [✓] │ │         Keyer Action         │
│ Lower Third - Worship    [✓] │ │ [          Go Live         ] │
│ Scripture Overlay        [ ] │ │ [       Go to Preview      ] │
│                              │ │ [         Keyer Off        ] │
│                              │ │ Keyer Behavior on Transition │
│                              │ │ [  Go Live at Scene Change ] │
│                              │ │ [ Keyer Off at Scene Change] │
│                              │ │  To leave the keyer unchanged│
│                              │ │  on scene transition, do not │
│                              │ │  select an option.           │
└──────────────────────────────┘ └──────────────────────────────┘
[+] [−]  [↑] [↓]
```

- **Check boxes** (right of each scene name) choose which keyer scenes are armed. Clicking a scene name only highlights it for the move and remove buttons.
- **Live scenes** are shown in red bold, **previewed scenes** in green bold.
- **Go Live** turns red when the checked scenes differ from what is live.
- **Go to Preview** only shows while Studio Mode is on, and stays green while a preview is showing.

## Several scenes at once

Any number of scenes can be checked. They go live together, layered in list order with the top of the list on top. Use the move up/down buttons or drag to change the order.

One scene goes on the keyer output as itself. Several scenes go on the output as a private scene that holds them as layers, so a keyer transition blends the whole group at once. This costs about the same as showing those scenes anyway.

## Keyer Action

| Button | What it does |
|---|---|
| **Go Live** | Puts the checked scenes on the keyer output now, with the keyer's transition. If a preview is showing, the previewed scenes go live instead and the preview is cleared. With nothing checked and nothing previewed, the keyer goes off. |
| **Go to Preview** | Studio Mode only. Shows the checked scenes on top of the preview scene. Click it again to clear the preview. |
| **Keyer Off** | Takes the keyer output off now. The check boxes and any preview stay as they are. It replaces the old pause (None) button. |

### How Go to Preview works

OBS has no way for a plugin to draw over the Studio Mode preview, so the checked scenes are added to the preview scene as a temporary, locked source at the top of its source list. While you preview, it shows in that scene's **Sources** list.

- **The preview follows the check boxes.** Checking or unchecking scenes updates the preview. Unchecking them all clears it.
- **It follows the preview scene.** If you pick another preview scene, it moves there.
- **Transitioning takes it live.** When the preview scene is transitioned to Program, the keyer scenes go along inside the scene. When the transition ends, the same picture moves to the keyer output and the temporary source is removed. This works with **Duplicate Scene** and **Swap Scenes** on or off.
- **It is never added to the scene that is on Program**, because that would put it on air. If the preview scene is the program scene, nothing shows until you pick another preview scene.
- **Leftovers are cleaned up.** If OBS closes or crashes while a preview is showing, the temporary source is removed the next time the scene collection loads.
- **Studio Mode off clears the preview**, and the button is hidden.

## Keyer Behavior on Transition

These replace the **Tie** check box. At most one can be on. Click a lit button again to turn it off.

| Setting | When the program scene transitions |
|---|---|
| **Go Live at Scene Change** | The checked scenes go live with the transition. If nothing is checked, or the checked scenes are already live, nothing changes. |
| **Keyer Off at Scene Change** | The keyer goes off with the transition. The check boxes stay, so Go Live brings the scenes back. |
| Neither selected | The keyer is left unchanged. |

On the main output, the keyer changes when the program transition **starts**, so the keyer and the scene transition run at the same time. Keyers on extra views or canvases change when their scene change is reported, as before.

The setting is saved per keyer. A keyer that had **Tie** on starts with **Go Live at Scene Change** on.

## Include Audio

Keyer audio is left out of the output mix by default. Check **Include Audio** in the gear menu to put a keyer's audio back.

- **Off (default).** The keyer output is wrapped in a private video-only source, so the scenes show but their sound is not mixed into the stream or recording. The sources still appear in the Audio Mixer with moving meters, because the mixer shows each source's own level, not what reaches the output. To hide them there, right-click the strip and choose **Hide**.
- **On.** The keyer's audio is mixed into the output, as it was before.
- **Shared sources.** If an audio source is in both a keyer scene and the program scene, leave Include Audio off. With it on, OBS mixes that source twice and it plays about twice as loud.
- Switching the option while scenes are live swaps the source without a transition, so the picture does not change.

## How it fits with the other options

- **Exclude Scene and Load On Scene still win.** On an excluded or unlisted program scene, the keyer output is off, and nothing is taken live from a preview. What was live comes back on the next scene the keyer is shown on.
- **Load On Scene auto-load** shows the scenes that were last live, or the first scene in the list, when a listed scene goes live and nothing is live.
- **Hide After** starts when scenes go live (Go Live, a transition, or a preview take). It does not run during a preview. When it runs out, the keyer goes off and the check boxes stay.
- **Go to Preview wins over the scene change setting.** If the previewed scene is taken to Program, the previewed keyer scenes go live, even with **Keyer Off at Scene Change** on.

## Hotkeys

- **Go Live** and **Go to Preview** hotkeys are added for each keyer.
- The existing per-scene **Show on** / **Hide on** hotkeys check or uncheck that scene and add it to, or remove it from, what is live right away.
- The **Hide on** keyer hotkey works like **Keyer Off**.
- The **Enable Tie** / **Disable Tie** hotkeys turn **Go Live at Scene Change** on and off.

## obs-websocket

Two vendor requests are added to the `downstream-keyer` vendor:

| Request | Description |
|---|---|
| `dsk_go_live` | Same as the **Go Live** button |
| `dsk_go_to_preview` | Same as **Go to Preview**, but it only starts a preview and never clears one |

| Field | Required | Description |
|---|---|---|
| `dsk_name` | yes | Name of the downstream keyer (tab name) |
| `scene` | no | Check only this keyer scene first |
| `view_name` | no | View/canvas name. Leave it out or empty for the main OBS output |

The response has `success`, plus `error` when `view_name` is unknown or `dsk_name` is missing.

Existing requests keep working:

- `dsk_select_scene` checks only that scene and puts it live right away. An empty `scene` turns the keyer off.
- `dsk_get_scene` returns the first live scene.
- `dsk_set_tie` turns **Go Live at Scene Change** on or off.
- The `dsk_scene_changed` event's `new_scene` lists the live scenes, separated by commas.

## Saved settings

Each keyer saves:

```json
"scenes": [
  { "name": "Lower Third - Pastor", "checked": true },
  { "name": "Scripture Overlay", "checked": false }
],
"live_scenes": [ { "name": "Lower Third - Pastor" } ],
"scene": "Lower Third - Pastor",
"scene_change_mode": 1,
"tie": true,
"include_audio": false
```

- `scene_change_mode`: `0` neither, `1` Go Live at Scene Change, `2` Keyer Off at Scene Change.
- `scene` and `tie` are still written for older versions and websocket clients.
- **Settings from earlier versions load as follows:** the scene that was on air stays live, nothing is checked, and `tie` becomes Go Live at Scene Change. Include Audio starts off, so existing keyers stop sending audio until it is checked.

## Known limitations

- **Layered scenes on extra canvases** are sized to the main canvas. On a canvas with a different resolution, the layers may be positioned wrong.
- **Go to Preview** is only available for the main output, because Studio Mode only exists there.
