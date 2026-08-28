# ===========================================================================
#  publish-version.ps1 -- announce a release to the update banner, and PROVE
#  it landed.
#
#  WHY THIS EXISTS
#  ---------------
#  docs/GITHUB.md gives release steps 1-5 a proof point each: open both files
#  and compare, read the exe's Properties, download from releases/latest
#  yourself. Step 6 -- "edit version.json on the server" -- had none, and on
#  2026-08-27 it was the step that was wrong in production:
#
#    * /version had been announcing 30.7.0 since Aug 26, while the GitHub
#      shelf still held v20.0.1. The banner's "Get it" button offered people
#      a release eleven versions older than the one it promised them.
#    * server/version.json in the tree said 19.1.1, deliberately left stale
#      by an earlier audit. A tracked file nobody owns reads as authoritative
#      and is not.
#
#  Version.h's own comment states the law this script obeys: nothing in a
#  COMPILER knows what version the WORK is at, so that fact needs a step in a
#  human process. The refinement is that /version is observable over plain
#  HTTPS -- so the step can be a real check that fails, not a checklist line
#  someone ticks.
#
#  WHAT IT CHECKS, in order, stopping at the first failure:
#    1. include/Version.h and installer/ticktimer.iss agree.   (the apply
#       check, same logic deploy-windows.bat already runs)
#    2. A GitHub release tagged vX.Y.Z actually exists. Announcing a version
#       the shelf does not hold is the exact failure above.
#    3. server/version.json's "latest" is REWRITTEN from Version.h -- never
#       hand-typed, so it cannot drift. Notes and url stay as authored.
#    4. The file is copied to the server.
#    5. The public /version endpoint is read BACK and must report the version
#       we just declared. This is the proof; everything before it is setup.
#
#  Run -VerifyOnly to perform 1, 2 and 5 without writing or copying anything.
#
#  ASCII ONLY, deliberately, like every script in tools/. Windows PowerShell
#  5.1 decodes a BOM-less .ps1 in the ANSI codepage, so a stray em dash here
#  becomes mojibake -- the same class of bug that made deploy-windows.bat
#  unrunnable (docs/TROUBLESHOOTING.md).
# ===========================================================================

[CmdletBinding()]
param(
    # Skip the writing and copying; just report whether the world is
    # consistent right now. Safe to run any time.
    [switch] $VerifyOnly,

    # Overridable so this script is not welded to one box. Defaults are the
    # live deployment; a Raspberry Pi migration changes these four lines and
    # nothing else.
    [string] $Repo       = 'LovedArie/TickTimer',
    [string] $SshTarget  = 'root@167.233.51.249',
    [string] $RemotePath = '/var/lib/ticktimer/version.json',
    [string] $PublicUrl  = 'https://ticktimer.perryouy.com/version'
)

$ErrorActionPreference = 'Stop'

# TLS 1.2 is not the default in Windows PowerShell 5.1, and GitHub refuses
# anything older. Without this line every web call below dies as a connection
# reset, which reads like a network problem and is not.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root       = Split-Path -Parent $PSScriptRoot
$VersionH   = Join-Path $Root 'include\Version.h'
$IssFile    = Join-Path $Root 'installer\ticktimer.iss'
$LocalJson  = Join-Path $Root 'server\version.json'

function Fail([string] $msg) {
    Write-Host ''
    Write-Host "  [X] $msg" -ForegroundColor Red
    Write-Host ''
    exit 1
}

function Step([string] $msg) { Write-Host "  $msg" }

Write-Host ''
Write-Host '  TickTimer  -  publishing the version announcement' -ForegroundColor Cyan
Write-Host '  ================================================='
Write-Host ''

# ---- 1. The two files that must agree ------------------------------------
# Same fact, same check, as deploy-windows.bat step 0. Duplicated on purpose:
# this script must be runnable on its own, and a check you can only get by
# running a DIFFERENT script is a check that gets skipped.
if (-not (Test-Path $VersionH)) { Fail "Cannot find $VersionH -- is this still the repo's tools\ folder?" }
if (-not (Test-Path $IssFile))  { Fail "Cannot find $IssFile." }

$treeVer = (Select-String -Path $VersionH -Pattern '^#define\s+TICKTIMER_VERSION_STRING\s+"([^"]+)"' |
            Select-Object -First 1).Matches.Groups[1].Value
$issVer  = (Select-String -Path $IssFile  -Pattern '^#define\s+AppVersion\s+"([^"]+)"' |
            Select-Object -First 1).Matches.Groups[1].Value

if ([string]::IsNullOrWhiteSpace($treeVer)) { Fail "Could not read TICKTIMER_VERSION_STRING out of include\Version.h." }
if ($treeVer -ne $issVer) {
    Fail ("APPLY CHECK FAILED - the tree disagrees with itself:`n" +
          "        include\Version.h        says  $treeVer`n" +
          "        installer\ticktimer.iss  says  $issVer`n`n" +
          "      These ship together in every drop. Fix the tree first;`n" +
          "      nothing was published.")
}
Step "Version check: tree and installer both say  $treeVer"

$tag = "v$treeVer"

# ---- 2. Does the shelf actually hold it? ---------------------------------
# THE check this script was written for. The banner's "Get it" button sends
# people to the releases page; announcing a version that is not there turns
# the banner into a promise the download cannot keep.
try {
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/tags/$tag" `
                             -Headers @{ 'User-Agent' = 'ticktimer-publish' } -TimeoutSec 25
} catch {
    Fail ("No GitHub release tagged $tag on $Repo.`n`n" +
          "      Publish the release FIRST, then run me. Announcing a version`n" +
          "      the shelf does not hold is exactly the bug this script exists`n" +
          "      to prevent: the banner offers an update that 404s.")
}
$assetCount = @($rel.assets).Count
if ($assetCount -eq 0) {
    Fail ("Release $tag exists but has NO attached files.`n" +
          "      The banner would send people to an empty release. Attach the`n" +
          "      installer (and the portable zip), then run me again.")
}
Step "GitHub release $tag exists, $assetCount file(s) attached."

if ($VerifyOnly) {
    Step 'VerifyOnly: skipping the write and the copy.'
} else {
    # ---- 3. Rewrite "latest" from Version.h ------------------------------
    # The number is GENERATED, never typed. Notes and url are authored
    # content and are left exactly as the repo holds them -- which is why
    # this file stays in version control: its prose belongs there, and the
    # git diff of this line is the record of what was announced when.
    if (-not (Test-Path $LocalJson)) { Fail "Cannot find $LocalJson." }
    $raw = Get-Content $LocalJson -Raw -Encoding UTF8
    try   { $info = $raw | ConvertFrom-Json }
    catch { Fail "server\version.json is not valid JSON. Fix it by hand, then re-run." }

    if ([string]::IsNullOrWhiteSpace($info.notes)) { Fail 'server\version.json has no "notes" line. Write one - it is what people read in the banner.' }
    if ([string]::IsNullOrWhiteSpace($info.url))   { Fail 'server\version.json has no "url". That is where the Get-it button sends people.' }

    # SURGICAL edit of the one value, not a ConvertTo-Json round trip. PS 5.1's
    # serialiser re-indents the whole file and turns every apostrophe into a
    # six-character unicode escape, so a round trip churns the diff on every
    # publish and shreds the readability of the _comment -- in a file whose
    # comment is half the point of keeping it in version control.
    # Replacing just this value keeps the diff to the one line that changed,
    # which is what makes `git log -p server/version.json` a usable record of
    # what was announced when.
    $updated = [regex]::Replace(
        $raw, '("latest"\s*:\s*")[^"]*(")', "`${1}$treeVer`${2}", 1)

    if ($updated -eq $raw -and $info.latest -ne $treeVer) {
        Fail 'Could not find a "latest" field to update in server\version.json.'
    }
    # Prove the surgery did not corrupt the document before it goes anywhere.
    try   { $check = $updated | ConvertFrom-Json }
    catch { Fail 'Rewriting "latest" produced invalid JSON. server\version.json was NOT modified.' }
    if ($check.latest -ne $treeVer) { Fail "Rewrote `"latest`" but it reads '$($check.latest)'. Aborting." }

    # UTF8Encoding($false) = no byte-order mark. Qt 6.11 happens to tolerate a
    # BOM here (measured, not assumed), but the server is handed raw bytes and
    # BOM-less is the one form no parser can object to.
    [System.IO.File]::WriteAllText($LocalJson, $updated, (New-Object System.Text.UTF8Encoding($false)))
    Step "server\version.json latest -> $treeVer"

    # ---- 4. Copy it to the box -------------------------------------------
    Step "Copying to $SshTarget..."
    & scp -o ConnectTimeout=15 $LocalJson "${SshTarget}:${RemotePath}"
    if ($LASTEXITCODE -ne 0) {
        Fail ("scp failed (exit $LASTEXITCODE). Nothing on the server changed.`n" +
              "      Check the box is up and your SSH key is loaded.")
    }
    Step 'Copied.'
}

# ---- 5. THE PROOF ---------------------------------------------------------
# Read the announcement back from the public internet, the same way the app
# will. Everything above is setup; this is the only step that proves it took.
# The server re-reads version.json on every request, so there is no cache to
# wait out and no restart to remember.
Step "Reading $PublicUrl back..."
try {
    $live = Invoke-RestMethod -Uri $PublicUrl -TimeoutSec 25
} catch {
    Fail ("Could not reach $PublicUrl.`n" +
          "      The file may be in place; it is NOT confirmed. Check the`n" +
          "      server is running before announcing anything to anyone.")
}
if ($live.latest -ne $treeVer) {
    Fail ("The server is still announcing '$($live.latest)', not '$treeVer'.`n" +
          "      The copy did not take effect. Do not tell anyone to update`n" +
          "      until this line reads $treeVer.")
}

Write-Host ''
Write-Host '  ========================================================'
Write-Host "  Confirmed. The server announces $treeVer" -ForegroundColor Green
Write-Host "      notes: $($live.notes)"
Write-Host "      url:   $($live.url)"
Write-Host "      shelf: $tag, $assetCount file(s)"
Write-Host '  ========================================================'
Write-Host ''
exit 0
