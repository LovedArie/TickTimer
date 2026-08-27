@echo off
setlocal enabledelayedexpansion
REM ===========================================================================
REM  deploy-windows.bat  --  make a portable TickTimer folder you can zip and
REM  send to anyone, no Qt install required on their machine.
REM
REM  WHAT IT DOES, and why each step exists:
REM   0. THE APPLY CHECK (v28.3): reads the version out of include\Version.h
REM      and out of installer\ticktimer.iss and refuses to continue if they
REM      disagree. Why this exists: TWICE now a drop has half-applied and
REM      looked finished (the lost v27; the half-applied v28.3 found sitting
REM      in the tree). A half-applied drop's most likely symptom is exactly
REM      these two files disagreeing - they ship together in every drop and
REM      the .iss says "bump BOTH" for the same reason. The check also prints
REM      the version LOUDLY so you can eyeball it against the drop's filename
REM      (ticktimer-vX.Y.Z-*.zip) before a stale tree becomes an installer.
REM   1. Configures + builds a RELEASE build (smaller, faster than Debug).
REM   2. RUNS THE TEST SUITES (v28.3) and hard-stops on red. The build step
REM      proves the code compiles; only the tests prove the rules still hold -
REM      and a red suite must never ride into an installer someone installs.
REM   3. Copies both exes into dist\TickTimer\.
REM   4. Runs windeployqt on each - THE key step: it scans the exe for the Qt
REM      DLLs it needs and copies them (plus plugins) right next to it. This is
REM      the exact thing that, done by hand, docs/RUNNING.md used to walk you
REM      through. Now it's automatic.
REM   5. Drops a plain-English README and two launcher shortcuts in the folder.
REM
REM  Just double-click this file. If it can't find things, it tells you what to
REM  fix rather than failing silently.
REM ===========================================================================

echo.
echo   TickTimer  -  building a portable package
echo   =========================================
echo.

REM --- The apply check (step 0 above) --------------------------------------
REM findstr /b anchors at line START, so the commented-out "#define AppVersion"
REM examples in the .iss (they begin with ';') can't match - only the live
REM define can. tokens=3 grabs the third whitespace-separated word, which is
REM the quoted version; %%~A strips the quotes (that's what the ~ does).
set "TREEVER="
set "ISSVER="
for /f "tokens=3" %%A in ('findstr /b /c:"#define TICKTIMER_VERSION_STRING" "%~dp0..\include\Version.h"') do set "TREEVER=%%~A"
for /f "tokens=3" %%A in ('findstr /b /c:"#define AppVersion" "%~dp0..\installer\ticktimer.iss"') do set "ISSVER=%%~A"

if not defined TREEVER (
    echo   [X] Couldn't read a version out of include\Version.h.
    echo       Am I still inside the project's tools\ folder? If the file
    echo       moved or the tree is damaged, fix that before deploying.
    pause
    exit /b 1
)
if not "%TREEVER%"=="%ISSVER%" (
    echo   [X] APPLY CHECK FAILED - the tree disagrees with itself:
    echo         include\Version.h        says  %TREEVER%
    echo         installer\ticktimer.iss  says  %ISSVER%
    echo.
    echo       These two ship together in every drop and must match. A
    echo       mismatch means either a drop only HALF-applied - unzip it
    echo       over the project root again, letting it overwrite - or a
    echo       hand edit bumped one file and forgot the other.
    echo       Nothing was built. Fix the tree first.
    pause
    exit /b 1
)
echo   Version check: tree and installer both say  %TREEVER%
echo   ^(Eyeball that against the drop you just applied - the zip's own
echo    filename carries its version.^)
echo.

REM --- Find Qt -------------------------------------------------------------
REM windeployqt lives inside your Qt install. We look in the usual C:\Qt spot
REM for a MinGW kit; if your Qt is elsewhere, set QTDIR before running, e.g.
REM   set QTDIR=D:\Qt\6.8.0\mingw_64  &  deploy-windows.bat
if defined QTDIR goto have_qt
for /d %%V in ("C:\Qt\6.*") do (
    for /d %%K in ("%%V\mingw_64" "%%V\mingw*_64") do (
        if exist "%%K\bin\windeployqt.exe" set "QTDIR=%%K"
    )
)
:have_qt
if not defined QTDIR (
    echo   [X] Could not find Qt automatically.
    echo       Set it yourself and re-run, for example:
    echo         set QTDIR=C:\Qt\6.8.0\mingw_64
    echo         deploy-windows.bat
    pause
    exit /b 1
)
echo   Using Qt at: %QTDIR%
set "WINDEPLOYQT=%QTDIR%\bin\windeployqt.exe"
set "MINGW_BIN=%QTDIR%\bin"

REM --- Find the MATCHING MinGW toolchain -----------------------------------
REM Qt for MinGW is built with Qt's OWN bundled MinGW (under C:\Qt\Tools),
REM NOT with any MSYS2 compiler you may also have installed. Mixing them is
REM the classic "compiler can't compile a simple program" failure: CMake
REM grabs whatever c++/make it finds first on PATH, and if that's an MSYS2
REM toolchain it won't match Qt. So we locate Qt's MinGW and force it.
set "MINGW_TOOL="
for /d %%V in ("C:\Qt\Tools\mingw*_64") do if exist "%%V\bin\g++.exe" set "MINGW_TOOL=%%V"
if defined MINGW_TOOL (
    echo   Using MinGW at: %MINGW_TOOL%
    REM Put Qt's MinGW FIRST on PATH so its g++/mingw32-make win over any
    REM MSYS2 tools. This is the whole fix for the cross-toolchain error.
    set "PATH=%MINGW_TOOL%\bin;%PATH%"
    set "CC=%MINGW_TOOL%\bin\gcc.exe"
    set "CXX=%MINGW_TOOL%\bin\g++.exe"
    set "MAKEPROG=%MINGW_TOOL%\bin\mingw32-make.exe"
) else (
    echo   [!] Could not find Qt's MinGW under C:\Qt\Tools.
    echo       If configure fails with a compiler error, install the MinGW
    echo       component in the Qt Maintenance Tool, or tell me your setup.
    set "MAKEPROG=mingw32-make"
)

REM --- Find CMake ----------------------------------------------------------
where cmake >nul 2>nul
if errorlevel 1 (
    REM Qt bundles a CMake under Tools\CMake; fall back to it.
    for /d %%V in ("C:\Qt\Tools\CMake*") do if exist "%%V\bin\cmake.exe" set "PATH=%%V\bin;!PATH!"
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo   [X] cmake not found on PATH and not in C:\Qt\Tools.
    echo       Open the "Qt 6.x (MinGW 64-bit)" command prompt and run me from there.
    pause
    exit /b 1
)

REM Put MinGW's own runtime DLLs on PATH so windeployqt can copy them too.
set "PATH=%MINGW_BIN%;%PATH%"

REM --- Build (Release) -----------------------------------------------------
set "ROOT=%~dp0.."
pushd "%ROOT%"
echo.
echo   Configuring...
REM A CMake cache remembers its compiler and REFUSES to switch on re-run -
REM so if a previous attempt cached the wrong (MSYS2) toolchain, we must
REM start clean or the fix above can't take effect.
if exist "build-release\CMakeCache.txt" (
    echo   Clearing stale build cache...
    rmdir /s /q "build-release"
)
REM Explicit compiler + make program = no room for CMake to pick the wrong
REM toolchain. -G "MinGW Makefiles" pairs with mingw32-make.
cmake -S . -B build-release -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM="%MAKEPROG%" ^
    -DCMAKE_C_COMPILER="%CC%" ^
    -DCMAKE_CXX_COMPILER="%CXX%" >nul
if errorlevel 1 ( echo   [X] Configure failed. & popd & pause & exit /b 1 )
echo   Building  (this takes a minute the first time)...
cmake --build build-release --config Release -j >nul
if errorlevel 1 ( echo   [X] Build failed. & popd & pause & exit /b 1 )

REM --- Run the suites (step 2 above) ---------------------------------------
REM ctest ships in the same bin as cmake, so the PATH work above already
REM found it. --output-on-failure prints the failing QTest's own detail, so
REM a red run tells you WHICH rule broke, not just that one did. The live
REM suite starts its own server from build-release - nothing to set up.
echo   Running the test suites  ^(six of them; takes a minute^)...
ctest --test-dir build-release --output-on-failure
if errorlevel 1 (
    echo.
    echo   [X] TESTS FAILED - stopping before anything reaches dist\.
    echo       The build compiled, but at least one rule the tests pin is
    echo       broken, and a red suite must not become an installer.
    echo       The first failing block above names the test - send it.
    popd
    pause
    exit /b 1
)
echo   All suites green.

REM --- Assemble dist\TickTimer --------------------------------------------
set "DIST=%ROOT%\dist\TickTimer"
if exist "%DIST%" rmdir /s /q "%DIST%" 2>nul
REM If the folder is still there, something running from it holds the files
REM open - Windows will not delete or replace a loaded exe/DLL. Continuing
REM would silently ship the OLD build, so this is a hard stop with the fix.
if exist "%DIST%" (
    echo.
    echo   [X] Can't rebuild the package: files in dist\TickTimer are IN USE.
    echo       Close TickTimer and the black server window - anything that
    echo       was started from the dist folder - then run me again.
    popd
    pause
    exit /b 1
)
mkdir "%DIST%"

copy /y "build-release\ticktimer.exe"        "%DIST%\" >nul
if errorlevel 1 ( echo   [X] Couldn't copy ticktimer.exe into dist. & popd & pause & exit /b 1 )
copy /y "build-release\ticktimer-server.exe" "%DIST%\" >nul
if errorlevel 1 ( echo   [X] Couldn't copy ticktimer-server.exe into dist. & popd & pause & exit /b 1 )

echo.
echo   Bundling Qt runtime (windeployqt)...
"%WINDEPLOYQT%" --release --no-translations "%DIST%\ticktimer.exe" >nul
"%WINDEPLOYQT%" --release --no-translations "%DIST%\ticktimer-server.exe" >nul

REM --- Launchers the person actually clicks -------------------------------
REM Two tiny .bat files with friendly names. The server one leaves its window
REM open (that window PRINTS THE ADDRESS she needs); the app one just runs.
REM
REM --bind any is load-bearing here (v30.4.1). Since v30.2.1 the server binds
REM LOCALHOST by default, which is the right default for a box on the public
REM internet and exactly wrong for THIS bundle: a portable zip exists so a
REM tester can run it on their own laptop and reach it from their own phone.
REM Without the flag that window prints "NOT reachable" and the phone simply
REM cannot connect. The server's own banner warns that an open signup is
REM listening on every interface, which is the honest thing to say about a
REM zip somebody is running on their home Wi-Fi.
> "%DIST%\Start TickTimer server.bat" echo @echo off
>>"%DIST%\Start TickTimer server.bat" echo cd /d "%%~dp0"
>>"%DIST%\Start TickTimer server.bat" echo echo Leave this window OPEN while you use TickTimer.
>>"%DIST%\Start TickTimer server.bat" echo echo (It shows the address to type on other devices.)
>>"%DIST%\Start TickTimer server.bat" echo echo.
>>"%DIST%\Start TickTimer server.bat" echo ticktimer-server.exe --bind any

> "%DIST%\TickTimer.bat" echo @echo off
>>"%DIST%\TickTimer.bat" echo cd /d "%%~dp0"
>>"%DIST%\TickTimer.bat" echo start "" ticktimer.exe

REM --- Bundle the human-facing readme -------------------------------------
if exist "%ROOT%\docs\FOR-TESTERS.md" copy /y "%ROOT%\docs\FOR-TESTERS.md" "%DIST%\READ ME FIRST.txt" >nul

popd
echo.
echo   ========================================================
echo   Done.  Your portable app is in:
echo       %DIST%
echo.
echo   Right-click that TickTimer folder -^> Send to -^> Compressed
echo   (zipped) folder, and send the .zip to whoever's testing.
echo   ========================================================
echo.
pause
