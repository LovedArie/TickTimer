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

## The release routine, forever after

1. Bump `include/Version.h` (one file — the exes and the check follow).
2. Build + zip (`deploy-windows.bat`).
3. `git add . && git commit -m "..." && git push`
4. GitHub → new release, tag `vX.Y.Z`, attach zip, publish.
5. Edit `version.json` on the server: `"latest": "X.Y.Z"`.

Everyone on the old version sees the banner at next launch. That's the
whole loop the networked arc was building toward.
