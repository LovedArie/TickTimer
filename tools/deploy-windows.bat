@echo off
setlocal enabledelayedexpansion
REM ===========================================================================
REM  deploy-windows.bat  --  make a portable TickTimer folder you can zip and
REM  send to anyone, no Qt install required on their machine.
REM
REM  WHAT IT DOES, and why each step exists:
REM   1. Configures + builds a RELEASE build (smaller, faster than Debug).
REM   2. Copies both exes into dist\TickTimer\.
REM   3. Runs windeployqt on each — THE key step: it scans the exe for the Qt
REM      DLLs it needs and copies them (plus plugins) right next to it. This is
REM      the exact thing that, done by hand, docs/RUNNING.md used to walk you
REM      through. Now it's automatic.
REM   4. Drops a plain-English README and two launcher shortcuts in the folder.
REM
REM  Just double-click this file. If it can't find things, it tells you what to
REM  fix rather than failing silently.
REM ===========================================================================

echo.
echo   TickTimer  -  building a portable package
echo   =========================================
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
REM A CMake cache remembers its compiler and REFUSES to switch on re-run —
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

REM --- Assemble dist\TickTimer --------------------------------------------
set "DIST=%ROOT%\dist\TickTimer"
if exist "%DIST%" rmdir /s /q "%DIST%" 2>nul
REM If the folder is still there, something running from it holds the files
REM open — Windows will not delete or replace a loaded exe/DLL. Continuing
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
> "%DIST%\Start TickTimer server.bat" echo @echo off
>>"%DIST%\Start TickTimer server.bat" echo cd /d "%%~dp0"
>>"%DIST%\Start TickTimer server.bat" echo echo Leave this window OPEN while you use TickTimer.
>>"%DIST%\Start TickTimer server.bat" echo echo (It shows the address to type on other devices.)
>>"%DIST%\Start TickTimer server.bat" echo echo.
>>"%DIST%\Start TickTimer server.bat" echo ticktimer-server.exe

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
