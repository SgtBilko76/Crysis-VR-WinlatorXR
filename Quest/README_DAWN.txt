CRYSIS VR - WinlatorXR DAWN PACKAGE - BETA 0.3
==============================================

For WinlatorXR "Dawn" (dawn-33 or newer). If you use the older cats-27 fork,
take CrysisVR-Installer-Beta-*.zip instead - that one has install.cmd.

You need your own copy of Crysis (2007, Steam or GOG - NOT Remastered).


WHAT IS IN HERE
---------------
  Bin32\                CrysisVR.exe launcher + haptics DLLs (32-bit: the one
                        the headset uses; Bin64 has no working D3D10 in Box64)
  Mods\VRMod\           the VR mod itself
  system.cfg            tuned startup settings (resolution 1792x1624, window
                        mode, low spec). Replaces Crysis' own system.cfg.
  crysisvr_quest.cfg    performance settings the mod applies while playing
  CrysisVR - proton-9.0-x86_64 - Quest 3.wxrprofile.json
                        the container settings, ready to import into Dawn


INSTALL
-------
1. Copy your Crysis folder to the headset, e.g.
     /sdcard/Download/Crysis

2. Extract everything from this zip INTO that folder, so that you get
     Crysis\Bin32\CrysisVR.exe
     Crysis\Mods\VRMod\...
     Crysis\system.cfg
   (Keep a copy of the original system.cfg if you want to go back.)

3. In WinlatorXR Dawn: create a shortcut for
     D:\Crysis\Bin32\CrysisVR.exe
   Make sure drive D: points to /sdcard/Download.

4. Import the profile: copy the .wxrprofile.json to
     /sdcard/Download/Winlator/WxrProfiles/
   then open the shortcut's settings in Dawn and import it. It sets the
   screen size (1792x1624), DXVK, the Turnip/wrapper driver and Box64.

5. Put the headset ON, then launch. First start takes several minutes
   (Box64 + shader compilation). If the headset reads as taken off, the
   Quest suspends the app and it looks frozen.


TUNING
------
Edit crysisvr_quest.cfg in the Crysis folder - the mod applies it at start and
after every menu, so it overrides the in-game graphics options.

  vr_winlatorxr_render_height   0 = full screen height (sharpest).
                                e.g. 1280 for more fps, blurrier.
  sys_spec_*                    quality groups, 1 = Low.
                                Keep Texture and Water at 3: low textures look
                                blurry and low water draws the ocean surface
                                across the sky in VR.

Smaller screen size (1592x1440 in the profile and system.cfg) gives a few more
fps at the cost of sharpness. Keep the ~1.10 aspect or the image is squeezed.


CONTROLS
--------
Same as the beta package - see README_QUEST.md in CrysisVR-Installer-Beta-0.3
or https://github.com/SgtBilko76/Crysis-QuestVR

Crysis is (C) Crytek / Electronic Arts.
Based on crysis_vrmod by fholger: https://github.com/fholger/crysis_vrmod
