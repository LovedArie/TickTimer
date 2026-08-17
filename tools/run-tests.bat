@echo off
setlocal enabledelayedexpansion
REM ===========================================================================
REM  run-tests.bat  --  run the test suites WITHOUT deploying.
REM
REM  WHY THIS EXISTS: the test exes link against Qt's DLLs, and a bare
REM  command prompt doesn't have Qt on PATH -- launching one there dies with
REM  "Qt6Gui.dll was not found". deploy-windows.bat solves that internally
REM  for its own ctest step; this script gives the same environment to a
REM  hand-run, so "run the tests by hand" (SETUP section 3) is actually a
REM  one-liner on Windows too.
REM
REM  USAGE:
REM    run-tests.bat            -> all six suites
REM    run-tests.bat ui         -> only suites matching "ui"
REM  Output goes to the console AND to test-results.txt in the project
REM  root -- that file is the thing to send when a suite goes red.
REM
REM  v28.3.4: REBUILDS changed files before testing. The first version ran
REM  ctest against whatever binaries existed -- so "unzip a fix, run the
REM  tests" silently re-tested YESTERDAY'S code (it happened; the tell was
REM  a failure message quoting a literal the fixed source no longer
REM  contains). An incremental build is seconds when little changed, and
REM  it makes the one command mean what it says: test the code on disk.
REM  No compiler discovery needed: CMake's cache remembers the compiler
REM  and make tool by absolute path from the last deploy-windows.bat run.
REM ===========================================================================

REM --- Find Qt (same discovery as deploy-windows.bat) ----------------------
if defined QTDIR goto have_qt
for /d %%V in ("C:\Qt\6.*") do (
    for /d %%K in ("%%V\mingw_64" "%%V\mingw*_64") do (
        if exist "%%K\bin\Qt6Core.dll" set "QTDIR=%%K"
    )
)
:have_qt
if not defined QTDIR (
    echo   [X] Could not find Qt automatically. Set it yourself and re-run:
    echo         set QTDIR=C:\Qt\6.11.1\mingw_64
    echo         run-tests.bat
    pause
    exit /b 1
)
set "PATH=%QTDIR%\bin;%PATH%"

REM --- Find ctest (ships beside cmake; Qt bundles one under Tools) ---------
where ctest >nul 2>nul
if errorlevel 1 (
    for /d %%V in ("C:\Qt\Tools\CMake*") do if exist "%%V\bin\ctest.exe" set "PATH=%%V\bin;!PATH!"
)
where ctest >nul 2>nul
if errorlevel 1 (
    echo   [X] ctest not found on PATH and not in C:\Qt\Tools.
    pause
    exit /b 1
)

set "ROOT=%~dp0.."
pushd "%ROOT%"
if not exist "build-release\CTestTestfile.cmake" (
    echo   [X] No test build found in build-release\.
    echo       Run deploy-windows.bat once first - it configures and builds.
    popd
    pause
    exit /b 1
)

echo   Rebuilding what changed  ^(so the tests run TODAY'S code^)...
cmake --build build-release -j >nul
if errorlevel 1 (
    echo   [X] Rebuild failed - the errors above name the file.
    echo       Fix that first, or run deploy-windows.bat for a clean build.
    popd
    pause
    exit /b 1
)

if "%~1"=="" (
    echo   Running ALL suites  ^(results also go to test-results.txt^)...
    ctest --test-dir build-release --output-on-failure > test-results.txt 2>&1
) else (
    echo   Running suites matching "%~1"  ^(results also go to test-results.txt^)...
    ctest --test-dir build-release -R "%~1" --output-on-failure > test-results.txt 2>&1
)
set "RESULT=%ERRORLEVEL%"
type test-results.txt
echo.
if "%RESULT%"=="0" (
    echo   All green. Full log: %ROOT%\test-results.txt
) else (
    echo   [X] Failures above. Send %ROOT%\test-results.txt
)
popd
pause
