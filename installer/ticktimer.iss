; ticktimer.iss  --  Inno Setup script for a real TickTimer installer.
;
; This is the STEP UP from the portable zip: instead of "unzip and find the
; .bat", the person runs TickTimer-Setup.exe, clicks Next a few times, and
; gets Start-menu + desktop shortcuts with proper icons, plus a clean
; uninstaller in "Add or remove programs". It's how finished Windows apps
; are shipped.
;
; HOW TO USE (once):
;   1. Install Inno Setup (free): https://jrsoftware.org/isdl.php
;   2. Run tools\deploy-windows.bat FIRST — this installer just packages
;      whatever is in dist\TickTimer\, so those files must already exist.
;   3. Open this file in the Inno Setup Compiler and press F9 (Compile+Run),
;      or run:  iscc installer\ticktimer.iss
;   4. Out comes  installer\Output\TickTimer-Setup.exe  — send THAT.
;
; Why a separate tool (Inno) instead of CMake/CPack? CPack can build an NSIS
; installer, but Inno is friendlier to read, needs no extra CMake plumbing,
; and its script is self-documenting — which fits the "learn what each layer
; does" goal better than a generated one you can't inspect.

#define AppName        "TickTimer"
; ---------------------------------------------------------------------------
; VERSION — the only hand-copied version number in the project, and (v26.8
; audit) it had drifted FIVE releases: this line said 21.2.0 while the app
; was at 26.8.0. Its own comment said "bump BOTH, every release". It didn't.
;
; That contrast is the whole lesson, and it is sitting right here in one
; folder. Version.h has THREE consumers:
;   * C++ code            - #includes the header          -> never drifted
;   * ticktimer.rc        - #includes the header          -> never drifted
;   * this script         - a human retypes the number    -> drifted 5 times
; The two mechanical consumers were correct for five straight releases. The
; one relying on discipline was wrong. Mechanism beats intention, every time,
; and a comment shouting MUST is not a mechanism.
;
; Consequence, and why this mattered beyond tidiness: Inno uses AppVersion for
; upgrade detection. An installer claiming 21.2.0 over an installed 26.x is
; not obviously an upgrade, so the "installing over the top" path was being
; tested against a lie.
;
; PROPOSED MECHANICAL FIX (deliberately NOT enabled - see below):
;   #define ExeForVersion DistDir + "\ticktimer.exe"
;   #ifexist ExeForVersion
;     #define AppVersion GetStringFileInfo(ExeForVersion, PRODUCT_VERSION)
;   #else
;     #define AppVersion "28.3.0"
;   #endif
; ticktimer.rc already stamps the exe FROM Version.h, so reading it back makes
; the installer derive from the one source instead of copying it - the same
; "derive, don't store" rule the domain code follows.
;
; It is left commented out for one reason: this script is the last thing
; between the project and a user's machine, and it cannot be exercised on the
; audit machine (no Windows, no Inno). Shipping an untested edit to the one
; artifact you cannot test is how a documentation cleanup becomes an outage.
; Enable it in a session where you can run ISCC and watch it build.
#define AppVersion     "30.2.1"  ; MUST match include/Version.h (Inno cannot include C headers - bump BOTH, every release)
#define AppPublisher   "TickTimer"
#define DistDir        "..\dist\TickTimer"   ; produced by deploy-windows.bat

[Setup]
AppId={{7C2D9E14-9B3A-4F5E-A6B1-TICKTIMER0001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
; Install into the user's profile (no admin prompt). A home app testing on a
; girlfriend's laptop shouldn't demand administrator rights.
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputBaseFilename=TickTimer-Setup
SetupIconFile=ticktimer.ico
UninstallDisplayIcon={app}\ticktimer.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create desktop shortcuts"; \
    GroupDescription: "Additional shortcuts:"

[Files]
; Grab the ENTIRE deployed folder — exes, every Qt DLL, plugins, the readme.
; recursesubdirs is what pulls in the plugins\ subfolders windeployqt makes.
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; Start-menu shortcuts. The server one is named so its job is obvious, and
; points at the exe directly (its own console window is the UI it needs).
Name: "{group}\TickTimer";               Filename: "{app}\ticktimer.exe"
Name: "{group}\Start TickTimer server";  Filename: "{app}\ticktimer-server.exe"
Name: "{group}\Read me first";           Filename: "{app}\READ ME FIRST.txt"
Name: "{group}\Uninstall TickTimer";     Filename: "{uninstallexe}"
; Optional desktop shortcuts (guarded by the task above).
Name: "{autodesktop}\TickTimer";               Filename: "{app}\ticktimer.exe";        Tasks: desktopicon
Name: "{autodesktop}\Start TickTimer server";  Filename: "{app}\ticktimer-server.exe"; Tasks: desktopicon

[Run]
; Offer to open the readme the moment install finishes — the first thing a
; tester needs is the "start the server first" instruction.
Filename: "{app}\READ ME FIRST.txt"; Description: "Open the getting-started notes"; \
    Flags: postinstall shellexec skipifsilent
