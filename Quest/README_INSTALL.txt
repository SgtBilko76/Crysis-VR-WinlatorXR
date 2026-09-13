CRYSIS VR (Meta Quest / WinlatorXR) - MOD INSTALLER - BETA 0.2
==============================================================

This package installs the Crysis VR mod + tuned settings onto an EXISTING
Crysis (2007) install inside a WinlatorXR container. It does NOT include the
game - you must provide your own legal copy of Crysis (Steam/GOG, not
Remastered).

Package contents:
  install.cmd                 - the installer (run inside the container)
  Mods\VRMod\                 - the VR mod (VRMod.dll for 32/64 bit, Game assets)
  Bin32\, Bin64\              - CrysisVR.exe launcher + haptics DLLs
  crysisvr_quest_settings.cfg - VR cvars the installer appends to system.cfg
  CrysisVR.desktop            - ready-made WinlatorXR launch shortcut template
  README_QUEST.md             - full walkthrough, controls, troubleshooting

--------------------------------------------------------------------------
WHAT THE INSTALLER DOES (automatable) vs. MANUAL STEPS (WinlatorXR UI only)
--------------------------------------------------------------------------
The installer copies the mod and launcher into your Crysis folder and applies
the VR settings. It CANNOT create/configure the container (drive mapping,
screen size, DXVK, driver) or the launch shortcut - those are WinlatorXR-only
and must be done by hand (steps below).

--------------------------------------------------------------------------
SETUP - do these in order
--------------------------------------------------------------------------
0) Use the "cats-27" WinlatorXR build. AVOID the "dawn" builds - they break
   the stereo pipeline.

1) Create a container in WinlatorXR and set:
     - Screen size : 1792x1624   (or 1591x1440 for smoother; keep ~1.10 aspect)
     - DX wrapper  : DXVK
     - Graphics    : wrapper / Turnip
     - Drive  D:   : mapped to /sdcard/Download
   (If you already have the Far Cry VR container, reuse it - same settings.)

2) Put your Crysis install at  D:\Crysis  (= /sdcard/Download/Crysis) so that
   D:\Crysis\Bin32\Crysis.exe exists. Only Bin32, Bin64, Game and the root
   files are needed (skip LogBackups, Mods\CrysisCoop, backup folders).

3) Copy this whole installer folder somewhere on D: (e.g. D:\CrysisVR-Setup),
   then run install.cmd INSIDE the container. Two ways:
     a) A one-shot WinlatorXR shortcut:
          Exec:      wine C:\windows\system32\cmd.exe
          Arguments: /c D:\CrysisVR-Setup\install.cmd
     b) Any file manager / terminal in the container -> run install.cmd
   Accept the default Crysis path (D:\Crysis) or type yours.

4) Create the launch shortcut in WinlatorXR (or import the included
   CrysisVR.desktop, set its container_id to your container):
     Exec:        wine D:\Crysis\Bin32\CrysisVR.exe
     Launch args: (none)
     screenSize:  1792x1624   (match the container)

   USE THE 32-BIT LAUNCHER (Bin32). WinlatorXR cats-27 only ships DXVK for
   32-bit programs; Bin64\CrysisVR.exe fails with "Failed to load ...VRMod.dll
   Error 126" because there is no 64-bit d3d10/d3d11/dxgi in the container.

5) Launch it. Wear the headset during boot (several minutes on first start).
   Intro videos and menus are flat on a virtual screen; stereo + head tracking
   start once a level is loaded.

--------------------------------------------------------------------------
CONTROLS (right-handed defaults)
--------------------------------------------------------------------------
  Right trigger = fire         Left trigger = use / pick up
  Right grip = holster/switch weapon (hip, shoulder, chest)
  Left grip  = grab weapon with off hand (two-handed aiming)
  A = reload (hold: fire mode)   B = grenade (tap cycle / hold throw)
  X = binoculars (panel at left hand; raise it and point to aim)
  Y = suit menu (tap: armor, hold: menu)
  Left menu button = game menu   Right stick click = free (WinlatorXR menu)
  Right stick = turn / 45 snap; up = jump, down = crouch/prone
  Left stick = move; click = sprint
  Physical swing with grip held = melee; scope to the eyes = zoom

--------------------------------------------------------------------------
NOTES / TROUBLESHOOTING
--------------------------------------------------------------------------
  - Performance: Crysis is CPU-bound and Box64 has to emulate it; expect
    20-40 fps. AER (alternate-eye) + render height 1200 are the defaults.
    Lower vr_winlatorxr_render_height / sys_spec for more speed.
  - No stereo / no head tracking: the mod forces the window borderless at
    (0,0) automatically; if it persists, relaunch, and make sure you are on
    cats-27 (not dawn).
  - Image squeezed: screen size isn't ~1.10 aspect - use 1792x1624/1591x1440.
  - Low fps everywhere: charge the headset >50% (it throttles when low).
  - Game log: C:\users\xuser\Documents\My Games\Crysis VR\Game.log inside the
    container; lines tagged [WinlatorXR] show how far the mod got.
  - bHaptics / ProTubeVR are disabled on the headset; controller rumble works.

Mod: crysis_vrmod (fholger) + WinlatorXR backend. Crysis (c) Crytek/EA -
bring your own legally-owned copy.
