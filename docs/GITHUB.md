# Putting TickTimer on GitHub (repo + your first Release)

Two separate things live on GitHub, and this guide does both:

1. **The repository** — your source code's home: history, backup, and the
   portfolio piece a career move wants to see.
2. **Releases** — the download shelf attached to that repo, where built
   zips live with stable URLs. This is what the update banner points at.

Time: ~20 minutes the first time, ~2 minutes every release after.

---

## Part 1 — one-time setup

### 1. Make a GitHub account & install Git
- Account: <https://github.com> (free).
- Git for Windows: <https://git-scm.com/download/win> — install with
  defaults. This gives you the `git` command and *Git Bash*.

### 2. Create the (empty) repository on GitHub
- GitHub → **+** (top right) → **New repository**.
- Name: `ticktimer`. Visibility: **your call** — *Public* shows it off
  (portfolio!), *Private* keeps it yours; Releases work either way, though
  private-repo download links only work for accounts you invite.
- **Don't** tick "Add a README" — the project already has one, and an
  empty repo makes the first push simpler.

### 3. Push your project (from the project folder)
Open *Git Bash* **in your project folder** (right-click → *Open Git Bash
here*), then:

```bash
git init                      # start tracking this folder
git add .                     # stage everything (.gitignore filters builds)
git commit -m "TickTimer v19 - app, server, tests, docs"
git branch -M main
git remote add origin https://github.com/YOUR-USERNAME/ticktimer.git
git push -u origin main
```

GitHub will ask you to sign in in a browser window the first time.

> The project now ships a **`.gitignore`** that keeps build folders,
> `dist/`, Qt Creator's `.user` files, and — importantly — any
> `server_data/` (real accounts and planners) OUT of the repo. Check the
> first `git add .` didn't pick up anything personal: `git status` before
> committing, always.

---

## Part 2 — cutting a release (repeat per version)

1. **Build the zip**: run `tools\deploy-windows.bat`, then zip
   `dist\TickTimer` → you have `TickTimer.zip`.
2. On your repo page: **Releases** (right sidebar) → **Draft a new
   release**.
3. **Tag**: `v19.0.0` — match `include/Version.h` exactly (tags are the
   version history of your *builds*, the way commits are of your code).
4. Title: `TickTimer 19.0.0`. Description: paste the highlights (the
   README's feature bullets are a fine start).
5. **Attach `TickTimer.zip`** (drag it into the assets box) → **Publish**.

Your download page now exists at a stable URL:

```
https://github.com/YOUR-USERNAME/ticktimer/releases/latest
```

`/latest` always redirects to the newest published release — which is
exactly why it's the right URL for the update banner: set once, correct
forever.

---

## Part 3 — connect the update banner (once)

In your **server's data directory** (where `accounts.json` lives), create
`version.json` — copy `server/version.example.json` and fill it in:

```json
{
  "latest": "19.0.0",
  "url": "https://github.com/YOUR-USERNAME/ticktimer/releases/latest",
  "notes": "Share & compare; editable server address; update notices."
}
```

No restart needed — the server re-reads the file on every check.

## The release routine, forever after — with proof points

A version change is an **act, not a side effect**: nothing in the build
invents a number, a human declares one. Each step below has a checkpoint
that PROVES it took — skip the proofs and you'll ship ghosts.

1. Bump the version in **TWO** places: `include/Version.h` (feeds the
   code, both exes, and the update check) **and** `installer/ticktimer.iss`
   (Inno can't include C headers — this is the one seam kept in sync by
   hand, and it has been forgotten before).
   *Proof: open both files, same number.*
2. Build + package: `tools\deploy-windows.bat` (app and server CLOSED —
   Windows won't replace running exes).
   *Proof: `dist\TickTimer\ticktimer.exe` → Properties → Details →
   File version matches.*
3. Compile the installer: open the `.iss` → F9.
   *Proof: run the new Setup — Add/Remove Programs shows the new number.*
4. `git add . && git commit -m "vX.Y.Z - ..." && git push`
5. GitHub → new release, tag `vX.Y.Z` (type it, then CLICK "Create new
   tag on publish"), attach the fresh zip **and** Setup.exe, publish.
   *Proof: download from `releases/latest` yourself — `releases/latest`
   serves whatever you last PUBLISHED, and announcing a version the shelf
   doesn't hold yet makes the banner promise what "Get it" can't deliver.*
6. Announce it: **`tools\publish-version.bat`**. No restart, and nothing
   to retype — it reads the number out of `Version.h`, rewrites the
   `latest` line of `server/version.json`, copies that to the box, and
   then reads the public `/version` back.
   *Proof: the script's own exit code. It stops before touching anything
   if `Version.h` and the `.iss` disagree, or if the GitHub release tag
   doesn't exist yet with files attached; and it fails at the end unless
   the server really is announcing the version you just declared. Run
   `tools\publish-version.bat -VerifyOnly` any time to re-check all of
   that without publishing.*

Everyone on the old version sees the banner at next launch. That's the
whole loop the networked arc was building toward.

**Why step 6 is a script and steps 1-5 aren't.** For a long time it read
"edit `version.json` on the server" and was the only step with no proof
point — and on 2026-08-27 it was the step that was wrong: `/version` had
been announcing 30.7.0 for a day while the shelf still held v20.0.1, so
the banner offered an update that 404'd. The same day, `TickTimer-Setup.exe`
was found carrying the 30.4.0 binaries under a 30.8.0 label, because Inno
takes `AppVersion` from the hand-copied literal in the `.iss` and never
from the exe it wraps. Both failures are one shape: **a version claim that
nothing checked.** Steps 1-5 end at artifacts you can open and eyeball;
step 6 ends at a live HTTPS endpoint, which is the one thing here a machine
can verify better than you can.
