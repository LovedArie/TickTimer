@echo off
REM ===========================================================================
REM  publish-version.bat  --  double-clickable wrapper around
REM  publish-version.ps1, which does the actual work.
REM
REM  WHY A WRAPPER AND NOT ONE BATCH FILE: this job has to parse JSON, make
REM  HTTPS calls to GitHub and to the server, and compare what came back.
REM  cmd.exe can do none of those, so a pure-batch version would spawn
REM  PowerShell five separate times, each with its own quoting hazard. Spawn
REM  it ONCE, deliberately, and keep the logic somewhere that has a JSON
REM  parser. The .bat survives so the tool is still a thing you double-click,
REM  like every other script in this folder.
REM
REM  -ExecutionPolicy Bypass applies to THIS invocation only; it does not
REM  change any machine setting. -NoProfile keeps a personal profile from
REM  changing behaviour between two machines running the same script.
REM
REM  Pass -VerifyOnly to check consistency without publishing:
REM      tools\publish-version.bat -VerifyOnly
REM
REM  ASCII ONLY. A single non-ASCII character here desynchronises cmd.exe's
REM  line-by-line read under code page 65001 and corrupts the whole parse -
REM  see docs/TROUBLESHOOTING.md, "a .bat script fails on nearly every line".
REM ===========================================================================

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish-version.ps1" %*
set "RC=%ERRORLEVEL%"

REM Only hold the window open on failure. A green run that pauses trains you
REM to dismiss the window without reading it, which defeats the proof line.
if not "%RC%"=="0" pause
exit /b %RC%
