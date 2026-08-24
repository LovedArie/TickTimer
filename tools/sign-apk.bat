@echo off
setlocal enabledelayedexpansion
:: ---------------------------------------------------------------------------
:: sign-apk.bat -- sign the release APK that the CMake/androiddeployqt build
:: produced, so the phone will install it.
::
:: WHY THIS EXISTS: docs/ANDROID.md describes signing as something Qt Creator
:: does when you tick "Sign package". That is true and it is not the only way,
:: and the command-line form is long enough that pasting it by hand fails --
:: which is exactly how this file came to be written.
::
:: THE PASSWORD IS NEVER AN ARGUMENT HERE, deliberately. apksigner prompts for
:: it on the console, so it stays out of your shell history, out of the process
:: list, and out of this file. Never add a --ks-pass line below.
:: ---------------------------------------------------------------------------

set "REPO=%~dp0.."
set "KEYSTORE=%USERPROFILE%\android-release-key.jks"
set "UNSIGNED=%REPO%\build-android\android-build\build\outputs\apk\release\android-build-release-unsigned.apk"

:: The version comes from the one place it is allowed to come from. Same
:: findstr trick deploy-windows.bat uses for its apply check -- Version.h is
:: the single source, and a hand-typed number here would be a fifth seam.
set "VERSION="
for /f "tokens=3" %%v in ('findstr /b "#define TICKTIMER_VERSION_STRING" "%REPO%\include\Version.h"') do (
    set "VERSION=%%~v"
)
if "%VERSION%"=="" (
    echo   [X] Could not read TICKTIMER_VERSION_STRING from include\Version.h
    exit /b 1
)
set "SIGNED=%REPO%\build-android\ticktimer-%VERSION%.apk"

:: Newest build-tools wins -- the SDK keeps several side by side.
set "APKSIGNER="
for /f "delims=" %%d in ('dir /b /ad /o-n "%LOCALAPPDATA%\Android\Sdk\build-tools" 2^>nul') do (
    if not defined APKSIGNER set "APKSIGNER=%LOCALAPPDATA%\Android\Sdk\build-tools\%%d\apksigner.bat"
)
if not defined APKSIGNER (
    echo   [X] No Android build-tools found under %LOCALAPPDATA%\Android\Sdk
    exit /b 1
)

if not exist "%UNSIGNED%" (
    echo   [X] No unsigned APK at:
    echo       %UNSIGNED%
    echo       Build it first ^(Qt Creator, or the qt-cmake Android build^).
    exit /b 1
)
if not exist "%KEYSTORE%" (
    echo   [X] No keystore at %KEYSTORE%
    echo       See docs\ANDROID.md, "Create the keystore".
    exit /b 1
)

echo   Signing TickTimer %VERSION%
echo   Type the keystore password when asked. Nothing is stored.
echo.
call "%APKSIGNER%" sign --ks "%KEYSTORE%" --out "%SIGNED%" "%UNSIGNED%"
if errorlevel 1 (
    echo.
    echo   [X] Signing failed - wrong password, or the keystore alias moved.
    exit /b 1
)

call "%APKSIGNER%" verify --print-certs "%SIGNED%" >nul 2>&1
if errorlevel 1 (
    echo   [X] Signed, but the result does not verify. Do not ship it.
    exit /b 1
)

echo.
echo   [OK] %SIGNED%
endlocal
