# Crysis VR — Meta Quest 3 (Standalone) via WinlatorXR — BETA 0.2

Play **Crysis (2007)** in VR **standalone on a Meta Quest 3**, via the
[`crysis_vrmod`](https://github.com/fholger/crysis_vrmod) mod running under
**WinlatorXR** (Wine + Box64 + DXVK on Android). No PC needed at runtime.

> This is an early beta. Crysis is a very CPU-heavy game and Box64 has to emulate
> it, so expect frame rates well below the headset's 72 Hz and rough edges — see
> **Known Issues** at the bottom. WinlatorXR's reprojection keeps head tracking
> smooth even when the game itself runs slowly.

The same `VRMod.dll` works on PC (OpenXR) and on the headset: it detects
WinlatorXR at start-up (Wine's `Z:` drive) and switches to WinlatorXR's XrAPI
protocol for tracking/input, composing the stereo frame into the game window.

---

## What you need

1. **Meta Quest 3** (should also work on Quest 2/Pro; only Quest 3 is tested).
2. **WinlatorXR** — the **`cats-27`** build (`WinlatorXR-cats-27.apk`).
   - ⚠️ Avoid the `dawn` builds — they break the stereo pipeline.
3. A **Crysis (2007) install** — a legitimate copy (Steam or GOG, *not*
   Remastered). You provide your own game files (~7 GB).
4. This package (`CrysisVR-Installer/`):
   - `Mods/VRMod/` — the VR mod (`Bin64/VRMod.dll`, `Bin32/VRMod.dll`, `Game/` assets)
   - `Bin64/`, `Bin32/` — `CrysisVR.exe` launcher + haptics DLLs (go into the game's Bin folders)
   - `install.cmd` — in-container installer
   - `crysisvr_quest_settings.cfg` — tuned settings appended to `system.cfg`
   - `crysisvr_quest.cfg` — performance settings (details, view distance, render height), copied to `D:\Crysis`
   - `CrysisVR.desktop` — the WinlatorXR shortcut template
5. A **USB cable** + **adb** on a PC (for copying files), or a file manager on
   the headset.

---

## Install

### 1. Install WinlatorXR (cats-27)
Sideload the `cats-27` APK:
```
adb install -r WinlatorXR-cats-27.apk
```
On first launch, **grant all permissions**, especially **All files access**
(Settings → Apps → WinlatorXR → Permissions → Files). Let first-time setup finish
(it extracts the system image).

### 2. Copy the game + package to the headset
Place the Crysis folder at **`/sdcard/Download/Crysis`** (this becomes
`D:\Crysis` inside Wine) and the package next to it:
```
adb push Crysis/            /sdcard/Download/Crysis/
adb push CrysisVR-Installer/    /sdcard/Download/CrysisVR-Setup/
adb push CrysisVR-Installer/CrysisVR.desktop  /sdcard/Download/Winlator/CrysisVR.desktop
```
You can leave out `LogBackups`, `Mods\CrysisCoop` and any `* - Kopie` backup
folders from the game copy.

### 3. Create the container
In WinlatorXR → **Containers** tab → **add a new container** (it'll be
**container 1** if it's your first). Set:

| Setting | Value | Why |
|---|---|---|
| **Screen size** | `1592x1440` | **Aspect must be ~1.10** (see note) |
| **Graphics driver** | wrapper / Turnip | The Adreno wrapper the fork ships |
| **DX wrapper** | **DXVK** | Direct3D 10 → Vulkan. The DXVK package must include `d3d10core.dll` (all recent ones do) |
| **Drive `D:`** | `/sdcard/Download` | So `D:\Crysis` resolves — **essential** |

> **Aspect rule:** WinlatorXR renders each eye into a *square* framebuffer, so
> the screen size **must keep ~1.10 width:height** (the headset's FOV aspect) or
> the image looks squeezed. Default: `1592x1440`; `1792x1624` is sharper but slower. **Bad:** anything near-square like `1660x1600`.

### 4. Run the installer inside the container
Create a one-shot shortcut (or use any file manager/terminal in the container):
```
Exec:       wine C:\windows\system32\cmd.exe
Arguments:  /c D:\CrysisVR-Setup\install.cmd
```
Accept the default game path (`D:\Crysis`). The installer copies the mod into
`D:\Crysis\Mods\VRMod`, the launcher into `D:\Crysis\Bin64` / `Bin32`, and
appends the tuned settings to `D:\Crysis\system.cfg`.

### 5. Point the shortcut at your container
Edit `/sdcard/Download/Winlator/CrysisVR.desktop` so `container_id` matches the
container you made (e.g. `container_id=1`). The shortcut already carries the
right `screenSize` and launches the **32-bit** game, `D:\Crysis\Bin32\CrysisVR.exe`.

> Use the 32-bit build. WinlatorXR `cats-27` installs DXVK only for 32-bit
> programs (`syswow64`); its 64-bit `system32` has no `d3d10`/`d3d11`/`dxgi`
> at all, so `Bin64\CrysisVR.exe` fails with "Failed to load ...VRMod.dll,
> Error 126: Module not found". The mod is installed for both, so switch the
> shortcut to `Bin64` only if your container has a 64-bit DXVK.

### 6. Launch
From WinlatorXR's **Shortcuts** tab, tap **CrysisVR** (or launch it from a
frontend). First boot takes several minutes (Box64 + shader compilation).
**Wear the headset** during boot — if it reads as off-face, the Quest suspends
the app and it freezes. The intro videos are shown flat on a virtual screen;
the menus and loading screens appear on a curved screen standing in the room,
and stereo + head tracking of the game world start once a level is loaded.

---

## Performance settings (`D:\Crysis\crysisvr_quest.cfg`)

The mod applies `crysisvr_quest.cfg` when the game starts and every time you return
to the game from a menu or loading screen, so it wins over the in-game graphics
options and the player profile. Edit it to trade detail for frame rate; delete it
to use the in-game options instead. The defaults:

| Setting | Value | Notes |
|---|---|---|
| `vr_winlatorxr_render_height` | `0` | Per-eye render height; `0` = full screen height (sharpest). E.g. `1080` for more fps, but blurrier |
| `sys_spec_*` | `1` | Quality groups on Low, which also turns shadows off |
| `sys_spec_Texture` | `3` | Textures stay on High; Low would make everything blurry |
| `r_TexturesFilteringQuality`, `r_DetailTextures` | `0`, `1` | Sharp texture filtering and detail textures despite Low shading |
| `e_view_dist_ratio` | `30` | Object view distance (Low default 40) |
| `e_view_dist_ratio_detail` / `_vegetation` | `10` | Detail objects and vegetation (Low default 15) |
| `e_lod_ratio` | `2` | Simpler models sooner (Low default 3) |
| `e_max_view_dst` | commented out | Far clipping plane in metres; uncomment to cut distant terrain |

## Other settings (already in `crysisvr_quest_settings.cfg`)

| Setting | Value | Notes |
|---|---|---|
| `vr_winlatorxr_aer` | `1` | **Alternate-eye rendering** — one full-resolution eye per frame |
| `vr_winlatorxr_max_fps` | `0` | Uncapped (WinlatorXR's own 72 fps cap quantises to 36/18 fps) |
| `vr_seated_mode` | `1` | Comfortable; crouch with the stick, not physically |
| `vr_height_offset` | `0.0` | Eye-height tweak (metres). Adjust to taste |
| `vr_turn_mode` | `1` | Snap turning |
| screen (`.desktop`) | `1592x1440` | Matches the container screen size |

All `vr_*` values can also be changed in-game from the **VR Settings** menu or
the console; they are saved to the profile's `game.cfg`.

Always play on **>50% battery** — the Quest power-throttles at low charge and
tanks the frame rate regardless of settings.

---

## Controls (Touch controllers, right-handed defaults)

| Input | Action |
|---|---|
| Right trigger | Fire |
| Left trigger | Use / pick up |
| Right grip | Holster / switch weapon (at hip, shoulder, chest) |
| Left grip | Grab weapon with off hand (two-handed aiming) |
| **A** | Reload (hold: fire mode) |
| **B** | Grenade (tap: cycle, hold: throw) |
| **X** | Binoculars (the zoomed view appears on a panel at your left hand: raise it in front of your eyes and point it to aim) |
| **Y** | Suit menu (tap: armor mode, hold: open suit menu, then move hand and release) |
| **Left menu button** | Game menu / objectives |
| Right stick | Turn (45° snap); push up = jump, down = crouch/prone |
| Left stick | Move; click = sprint |
| Right stick **click** | *unbound* — reserved for the WinlatorXR menu |
| Physical swing with grip held | Melee |
| Scope to the eyes while two-handing | Weapon zoom |

Differences to the PC bindings: the suit menu moved from the right stick click
to **Y** and the game menu to the **left menu button**, because WinlatorXR
reserves the right stick click and maps the menu button to Esc.

In the menus, point the **right controller** at the curved screen to move the
mouse cursor: **trigger = click**, **left menu button = Esc/back**.

---

## Known issues (beta)

- **Performance.** Crysis is CPU-bound even on desktop hardware; under Box64
  expect roughly 20–40 fps depending on the scene and settings. AER and the
  low render height are the defaults for that reason. Head tracking stays
  smooth thanks to WinlatorXR's reprojection, but fast motion will judder.
- **AER** gives full per-eye resolution but each eye refreshes at half the
  composite rate; set `vr_winlatorxr_aer 0` for side-by-side if you prefer.
- **Vehicle HUD** is approximated as a flat panel in the aim direction.
- **Binoculars, weapon scopes and 2D cutscenes** are shown like on PC: as a panel
  in a head-tracked view (binoculars at your left hand, scopes and cutscenes
  fixed in front of you) over a black background. The panel always faces you.
  `vr_winlatorxr_2d_panel 0` restores the old flat WinlatorXR screen.
- **Menus and loading screens** stand 5 m in front of you on a curved screen
  (placed where you look when the menu opens). `vr_winlatorxr_menu_curve_radius`
  sets the curvature (0 = flat); `vr_winlatorxr_menu_panel 0` restores the old
  flat WinlatorXR screen that follows your head.
- **Resolution ceiling:** keep the ~1.10 aspect (e.g. `1592x1440` or
  `1792x1624`). The game is a 32-bit process here, so do not exceed ~1792 wide
  and keep `vr_winlatorxr_render_height` moderate (address space).
- **WinlatorXR container backup/export** is unreliable — hence these manual
  setup steps rather than a prefix image.
- bHaptics / ProTubeVR are disabled on the headset (controller vibration works).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Image **squeezed/stretched** | Screen size isn't ~1.10 aspect — use `1592x1440` |
| **No stereo / no head tracking** (flat, frozen view in a level) | The window must be borderless at (0,0) so WinlatorXR finds the sync pixel — the mod forces this automatically; if it persists, relaunch. Avoid the dawn builds. |
| Game never gets past a black window | DXVK package lacks D3D10 (`d3d10core.dll`) — pick another DXVK version in the container settings |
| "Failed to load ...VRMod.dll, Error 126" | You launched `Bin64`; this WinlatorXR build has no 64-bit Direct3D. Use `Bin32\CrysisVR.exe` |
| Freezes on a static frame | Headset read as off-face — put it on; the app was suspended |
| Very low fps everywhere | Battery low (throttling) — charge to >50%; lower `vr_winlatorxr_render_height` / `sys_spec` |
| `D:\Crysis` not found / won't launch | Container `D:` drive isn't mapped to `/sdcard/Download` |
| Menu opens and immediately closes | Set `vr_winlatorxr_block_desktop_input 1` (blocks WinlatorXR's emulated Esc in-game) |
| Buttons do nothing in-game | Check `D:\Crysis\Game.log` for `[WinlatorXR] Received first head pose`; if missing, WinlatorXR isn't sending XrAPI packets — relaunch, make sure the `cats-27` build is installed |

The mod logs everything WinlatorXR-related with a `[WinlatorXR]` prefix in the
game log (`D:\Crysis\Game.log`, pull it with
`adb pull /sdcard/Download/Crysis/Game.log`).

---

*Mod: `crysis_vrmod` (fholger) + WinlatorXR. Crysis © Crytek/EA — bring your
own legally-owned copy.*
