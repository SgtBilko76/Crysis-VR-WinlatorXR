@echo off
setlocal enabledelayedexpansion
REM ============================================================================
REM  Crysis VR (WinlatorXR) - in-container installer
REM  Run this INSIDE your WinlatorXR container (wine cmd /c install.cmd).
REM  It installs the VR mod + tuned settings onto your EXISTING Crysis install.
REM  It does NOT contain the game - you provide your own legal Crysis (2007) copy.
REM ============================================================================
set "SRC=%~dp0"

echo(
echo  Crysis VR - WinlatorXR mod installer
echo  ------------------------------------
echo(
set "CRYDIR=D:\Crysis"
set /p "CRYDIR=Path to your Crysis install [%CRYDIR%]: "
if "%CRYDIR%"=="" set "CRYDIR=D:\Crysis"

if not exist "%CRYDIR%\Bin64\Crysis.exe" (
  if not exist "%CRYDIR%\Bin32\Crysis.exe" (
    echo(
    echo  ERROR: Crysis not found at "%CRYDIR%\Bin64\Crysis.exe" or "%CRYDIR%\Bin32\Crysis.exe".
    echo  Install Crysis there first, then re-run this installer.
    echo(
    pause
    exit /b 1
  )
)

echo(
echo  Installing VR mod into "%CRYDIR%\Mods\VRMod" ...
if not exist "%CRYDIR%\Mods\VRMod" mkdir "%CRYDIR%\Mods\VRMod"
xcopy /E /I /Y "%SRC%Mods\VRMod" "%CRYDIR%\Mods\VRMod" >nul
if errorlevel 1 (
  echo  ERROR: failed to copy the mod files.
  pause
  exit /b 1
)

echo  Installing launcher and runtime DLLs ...
if exist "%CRYDIR%\Bin64" (
  copy /Y "%SRC%Bin64\*" "%CRYDIR%\Bin64\" >nul
)
if exist "%CRYDIR%\Bin32" (
  copy /Y "%SRC%Bin32\*" "%CRYDIR%\Bin32\" >nul
)

echo  Applying VR settings to "%CRYDIR%\system.cfg" ...
if not exist "%CRYDIR%\system.cfg" type nul > "%CRYDIR%\system.cfg"
findstr /i /c:"vr_winlatorxr_aer" "%CRYDIR%\system.cfg" >nul 2>&1
if errorlevel 1 (
  type "%SRC%crysisvr_quest_settings.cfg" >> "%CRYDIR%\system.cfg"
  echo  VR settings appended.
) else (
  echo  VR settings already present - left as-is ^(edit system.cfg to change^).
)

echo(
echo  ============================================================
echo   Mod installed. REMAINING MANUAL STEPS ^(WinlatorXR UI^):
echo(
echo   1^) Container settings for THIS container:
echo        - Screen size : 1792x1624  ^(or 1591x1440; keep ~1.10 aspect^)
echo        - DX wrapper  : DXVK ^(must include d3d10core.dll^)
echo        - Graphics    : wrapper / Turnip
echo        - Drive D:    : mapped to /sdcard/Download
echo   2^) Create a shortcut to:  wine %CRYDIR%\Bin32\CrysisVR.exe
echo        ^(a ready CrysisVR.desktop template is in this folder^)
echo   3^) Use the cats-27 WinlatorXR build ^(avoid dawn-line builds^).
echo(
echo   See README_QUEST.md for the full walkthrough.
echo  ============================================================
echo(
pause
endlocal
