@echo off
setlocal enabledelayedexpansion
REM ===========================================================================
REM  build-wasm.bat  --  build the WebAssembly app and lay out a servable
REM                      folder in build-wasm\serve\.
REM
REM  WHY THIS EXISTS: the WASM build needs three things on PATH that no other
REM  build in this project needs — the Emscripten SDK, a Ninja, and the
REM  wasm_singlethread Qt kit (which is a DIFFERENT Qt from the desktop one).
REM  Finding that combination once and then not writing it down is how a build
REM  becomes "the thing only that one afternoon could do".
REM
REM  USAGE:  tools\build-wasm.bat
REM  OUTPUT: build-wasm\serve\  — copy its CONTENTS to /var/www/ticktimer-app
REM          on the server (see deploy\Caddyfile.example, the /app block).
REM ===========================================================================

set "ROOT=%~dp0.."

REM --- 1. Emscripten ---------------------------------------------------------
REM Qt pins the version it was built against and refuses a mismatch with a
REM FATAL_ERROR naming both numbers, which is the good kind of failure.
if not exist "C:\emsdk\emsdk_env.bat" (
    echo   [X] Emscripten SDK not found at C:\emsdk
    echo       Install it:  git clone https://github.com/emscripten-core/emsdk
    echo                    cd emsdk ^&^& emsdk install 4.0.7 ^&^& emsdk activate 4.0.7
    exit /b 1
)
call "C:\emsdk\emsdk_env.bat" >nul 2>nul

REM --- 2. Ninja --------------------------------------------------------------
REM The Qt kits ship no Ninja and Emscripten cannot use MSVC's generator.
where ninja >nul 2>nul
if errorlevel 1 (
    if exist "C:\msys64\ucrt64\bin\ninja.exe" (
        set "PATH=C:\msys64\ucrt64\bin;!PATH!"
    ) else (
        echo   [X] ninja not found. Install it, or add its folder to PATH.
        exit /b 1
    )
)

REM --- 3. The WASM Qt kit ----------------------------------------------------
REM NOT the desktop kit: a separate install, usually a different Qt version.
set "QTWASM="
for /d %%V in ("C:\Qt\6.*") do (
    if exist "%%V\wasm_singlethread\bin\qt-cmake.bat" set "QTWASM=%%V\wasm_singlethread"
)
if not defined QTWASM (
    echo   [X] No Qt for WebAssembly kit found under C:\Qt.
    echo       Qt Maintenance Tool -^> Add components -^> Qt ^> ^<version^> ^> WebAssembly
    exit /b 1
)
echo   Using Qt kit: %QTWASM%

REM --- 4. Configure and build ------------------------------------------------
if not exist "%ROOT%\build-wasm\CMakeCache.txt" (
    echo   Configuring...
    call "%QTWASM%\bin\qt-cmake.bat" -G Ninja -S "%ROOT%" -B "%ROOT%\build-wasm" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)
echo   Building  ^(first time takes a few minutes^)...
cmake --build "%ROOT%\build-wasm"
if errorlevel 1 exit /b 1

REM --- 5. Lay out the servable folder ----------------------------------------
REM The shell page, the manifest and the icons are OURS (web\); the .js/.wasm
REM and qtloader.js are the build's. Both halves are needed and neither is
REM useful alone, so the layout happens here rather than in a deploy note
REM somebody has to remember.
set "OUT=%ROOT%\build-wasm\serve"
if not exist "%OUT%" mkdir "%OUT%"
copy /Y "%ROOT%\web\index.html"            "%OUT%\" >nul
copy /Y "%ROOT%\web\manifest.webmanifest"  "%OUT%\" >nul
if not exist "%OUT%\icons" mkdir "%OUT%\icons"
copy /Y "%ROOT%\web\icons\*.png"           "%OUT%\icons\" >nul
copy /Y "%ROOT%\build-wasm\ticktimer.js"   "%OUT%\" >nul
copy /Y "%ROOT%\build-wasm\ticktimer.wasm" "%OUT%\" >nul
copy /Y "%ROOT%\build-wasm\qtloader.js"    "%OUT%\" >nul

echo.
echo   Done. Servable app is in:
echo     %OUT%
echo.
echo   Try it locally:   cd "%OUT%" ^&^& python -m http.server 8099
echo                     then open http://localhost:8099/
echo.
echo   Deploy:           copy the CONTENTS of that folder to
echo                     /var/www/ticktimer-app on the server.
echo.
echo   FIRST THING TO CHECK, every time: write something, RELOAD the page,
echo   and confirm it is still there. That is the persistent-storage mount
echo   (web\index.html) doing its job — without it the app looks perfect and
echo   forgets everything.
