@echo off
REM ============================================================================
REM  Assembles the Meta Quest / WinlatorXR package into Quest\CrysisVR-Installer\
REM  from the built mod (Bin64\VRMod.dll, Bin32\VRMod.dll) and launcher
REM  (Bin64\CrysisVR.exe, Bin32\CrysisVR.exe), the same way the PC installer
REM  assembly works (Installer\create_installer.bat), and zips it as
REM  Quest\CrysisVR-Installer-Beta-<version>.zip (Far Cry VR Quest beta layout).
REM  Build the solution (both platforms) and the c1-launcher first.
REM ============================================================================
setlocal
set VERSION=0.2
set CURRENT_DIR=%~dp0
set PACKAGE_DIR=%CURRENT_DIR%CrysisVR-Installer
set ZIP_FILE=%CURRENT_DIR%CrysisVR-Installer-Beta-%VERSION%.zip
if exist "%PACKAGE_DIR%" (rmdir /S /Q "%PACKAGE_DIR%")
if exist "%ZIP_FILE%" (del /Q "%ZIP_FILE%")

mkdir "%PACKAGE_DIR%\Bin64"
mkdir "%PACKAGE_DIR%\Bin32"

set CRYSIS_INSTALL_DIR=%PACKAGE_DIR%
call "%CURRENT_DIR%..\install.bat"
call "%CURRENT_DIR%..\install32.bat"

copy /Y "%CURRENT_DIR%README_QUEST.md" "%PACKAGE_DIR%\"
copy /Y "%CURRENT_DIR%README_INSTALL.txt" "%PACKAGE_DIR%\"
copy /Y "%CURRENT_DIR%install.cmd" "%PACKAGE_DIR%\"
copy /Y "%CURRENT_DIR%crysisvr_quest_settings.cfg" "%PACKAGE_DIR%\"
copy /Y "%CURRENT_DIR%CrysisVR.desktop" "%PACKAGE_DIR%\"

powershell -NoProfile -Command "Compress-Archive -Path '%PACKAGE_DIR%' -DestinationPath '%ZIP_FILE%' -CompressionLevel Optimal -Force"

echo.
echo Package assembled in "%PACKAGE_DIR%"
echo Release zip: "%ZIP_FILE%"
echo Push to the headset with:
echo   adb push "%PACKAGE_DIR%" /sdcard/Download/CrysisVR-Setup
echo   adb push "%PACKAGE_DIR%\CrysisVR.desktop" /sdcard/Download/Winlator/
endlocal
