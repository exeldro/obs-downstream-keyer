# Load On Scene

**Load On Scene** is a per-keyer option that limits a downstream keyer's output to a chosen set of program scenes. While a listed scene is live in Program, the keyer shows its selected scene. On any other program scene, the keyer output is off.

It is the opposite of **Exclude Scene**:

| Option | Keyer output is shown on… |
|---|---|
| Exclude Scene | every program scene **except** the listed ones |
| Load On Scene | **only** the listed program scenes |

## Usage

1. In the **Downstream Keyer** dock, select the keyer tab you want to configure.
2. Click the gear (config) button and open the **Load On Scene** submenu.
3. Check each program scene the keyer should be shown on. Click a checked scene again to remove it.

Notes on how it behaves:

- **An empty list turns the feature off.** If no scenes are checked, the keyer behaves exactly as before and is shown on every program scene (except excluded ones).
- **The keyer's selected scene is kept.** When you switch to a program scene that isn't in the list, the keyer output is cleared, but its selection in the dock stays the same. The output comes back as soon as a listed scene goes live again.
- **Exclude Scene wins.** If a scene is both excluded and in the Load On Scene list, the keyer output is hidden on that scene.
- **Changes apply right away.** Adding or removing a scene re-checks the scene that is currently in Program, so you don't need to switch scenes to see the effect.
- **Each keyer has its own list.** This includes keyers on extra views or canvases. For those, "program scene" means the scene that is active on that view or canvas, looking through any transition in progress.
- **The list is saved per keyer** with the rest of the keyer settings (under `load_scenes`).

## obs-websocket

Two vendor requests are added to the `downstream-keyer` vendor. They match the existing `dsk_add_exclude_scene` / `dsk_remove_exclude_scene` requests.

| Request | Description |
|---|---|
| `dsk_add_load_scene` | Add a program scene to a keyer's Load On Scene list |
| `dsk_remove_load_scene` | Remove a program scene from a keyer's Load On Scene list |

Request fields:

| Field | Required | Description |
|---|---|---|
| `dsk_name` | yes | Name of the downstream keyer (tab name) |
| `scene` | yes | Name of the program scene |
| `view_name` | no | View/canvas name. Leave it out or empty for the main OBS output |

Response fields:

| Field | Description |
|---|---|
| `success` | `true` if a keyer named `dsk_name` was found |
| `error` | Set when `view_name` is unknown or a required field is missing |

Example (obs-websocket v5 `CallVendorRequest`):

```json
{
  "op": 6,
  "d": {
    "requestType": "CallVendorRequest",
    "requestId": "1",
    "requestData": {
      "vendorName": "downstream-keyer",
      "requestType": "dsk_add_load_scene",
      "requestData": {
        "dsk_name": "Downstream Keyer 1",
        "scene": "Interview"
      }
    }
  }
}
```

## Saved settings

Each keyer stores its list as an array of objects:

```json
"load_scenes": [
  { "name": "Interview" },
  { "name": "Wide Shot" }
]
```

Settings saved by an earlier development version of this feature used a `program_scene` key instead of `name`. They are still read when loading, and the next save rewrites them in the format above.
