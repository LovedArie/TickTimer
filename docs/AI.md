# Using the AI in TickTimer

*The user guide: what the AI features do, how to turn them on, what they cost,
and what leaves your machine. For the design reasoning behind all of this,
see `design-addendum-provider.md` and `design-addendum-chat.md`.*

Everything here is **off until you configure it**. TickTimer never contacts
an AI service on its own — no key, no requests, full stop.

---

## 1. The two AI features

**Smart quick-add (Ctrl+Enter).** The capture bar (Ctrl+N) already parses
lines like `lab 4 friday 5pm urgent weekly #school` by itself, offline, for
free — that never needs a key. Pressing **Ctrl+Enter** instead sends the line
to an AI model, which handles the messy phrasings the built-in parser can't
("the lab thing, end of next week probably, it's important"). One line goes
out; a task comes back.

**The Assistant (✦ in the left rail).** A chat page that can *see* your day —
today's plan, tracked time, tasks due — and answer questions about it: "How
is my day going?", "What should I work on next?", "What's overdue?".
It can **propose one kind of change**: moving a block you already missed,
into one of the slots the app worked out for it — you see a card and tap
Apply or Discard, and nothing happens until you do. Everything else it
cannot do: it cannot add, complete, or delete anything, cannot touch a
deadline or an estimate, and cannot move a block you might still get to. If
you ask, it will say so and hand you a quick-add line to paste instead.

Both features use the **same provider and the same key**, set once in
Settings → AI quick-add.

---

## 2. Pick a provider

| Provider | Key needed | Cost | Good to know |
|---|---|---|---|
| **Anthropic** | yes (`sk-ant-…`) | pay-per-use, cents/month at this usage | default model: `claude-haiku-4-5` |
| **OpenAI** | yes (`sk-…`) | pay-per-use | default: `gpt-4o-mini` |
| **Groq** | yes (`gsk_…`) | **free tier available** | very fast; default: `llama-3.1-8b-instant` |
| **Ollama (local)** | **no key** | **free** | runs on your machine; nothing leaves it |
| **Custom endpoint** | depends | depends | LM Studio, OpenRouter, your own server |

Rules of thumb:

- **Cheapest start with zero install:** Groq (free tier, one sign-up).
- **Maximum privacy:** Ollama — the model runs on your computer, so nothing
  is ever sent anywhere. Chat replies take longer (up to ~30 s on a laptop
  CPU); the app waits up to 60 s before giving up.
- **Best quality:** Anthropic or OpenAI, for pocket change at this usage.

---

## 3. Set it up (2 minutes)

1. Get a key from your provider's website (skip for Ollama):
   - Anthropic: console.anthropic.com → API keys
   - OpenAI: platform.openai.com → API keys
   - Groq: console.groq.com → API keys
2. In TickTimer: **⚙ Settings → AI quick-add**.
3. Pick the provider in the dropdown.
4. Paste the key. Leave **Model** blank unless you have a reason (see §7).
5. Press **Test**. You'll get **✓ Connected** — or an error that names the
   problem *and* the fix (see §8). Test fires one tiny request ("reply with
   OK") and works on what you've typed, even before saving.
6. Press **OK**. Done — Ctrl+Enter and the Assistant both work now.

Keys are remembered **per provider**, so switching between vendors never
means re-pasting. Cancel discards everything, including keys you typed.

**Developers:** instead of Settings, you can export the provider's
environment variable — `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GROQ_API_KEY`,
or `LLM_API_KEY` for a custom endpoint. Settings wins if both exist.

---

## 4. Ollama — the free, local, no-key option

1. Install from ollama.com (Windows/macOS/Linux).
2. In a terminal: `ollama pull llama3.2` (about 2 GB, once).
3. In TickTimer: Settings → AI quick-add → provider **Ollama (local)** →
   **Test** → OK. No key field needed — the app knows not to ask.

If you pulled a different model, put its exact name in the Model field
(`ollama list` shows what you have).

---

## 5. Custom endpoints (LM Studio, OpenRouter, your own)

Pick **Custom endpoint…** and two extra fields appear:

- **Address** — the server's base URL, e.g. `http://localhost:1234` for
  LM Studio. *Include any path prefix the service needs* (Groq-style
  services sometimes serve under `/openai`); the app appends
  `/v1/chat/completions` or `/v1/messages` itself.
- **Dialect** — which protocol the server speaks. When unsure, choose
  **OpenAI-compatible**: LM Studio, OpenRouter, Together, Groq and Ollama
  all speak it.

The Model field is **required** here (the app can't guess a stranger's
model names). Then **Test**.

The repository also ships `stub_llm.py` — a tiny fake provider for testing
the whole pipeline with no account at all: run it, point a Custom endpoint
at `http://127.0.0.1:8930`, dialect OpenAI-compatible.

---

## 5b. Assistant style (v25.3)

Settings → AI has an **Assistant style** row: **Calm** (the default — what
the assistant sounded like before this setting existed), **Brief** (one
sentence when one will do), **Coach** (direct, pushes kindly toward your
next block), or **Custom**.

**Extra instructions** is one short line that adds to any of them — e.g.
*"call me Sam, skip greetings"*. Pick Custom and that line becomes the whole
style. Keep it short on purpose: a long character description makes models
worse at following the actual rules, which is why the field is one line.

Two things no style can change: the assistant **never shames** (an empty
day is a fresh start, untracked time is information), and it **stays in its
lane** — it is a planner, not a counsellor, and it will not push if you seem
to be struggling. Those sit above every style, including yours. Style
applies to the Assistant page only; quick-add parsing has no tone to set.

## 5c. A fallback seat (v26)

Settings → AI has an **If unreachable, try** row for the Assistant: pick a
second provider (a local Ollama is the natural choice) and the chat falls
back to it when the first one cannot be *reached* — connection refused,
network down, timeout. The transcript says so as it happens ("⚠ Anthropic
unreachable — trying Ollama (local)…") and a fallback answer is labelled
with who wrote it.

What it will **not** do: paper over a configuration problem. A rejected key,
a wrong address (404), a rate limit — those fail loudly on the seat that
produced them, because you need to hear about them. And after an outage the
app remembers for ~20 seconds which seats were down, so offline messages
fail instantly instead of hanging on a dead connection each time.

Quick-add has no fallback row: when its AI is unreachable you already have
the deterministic parse — that *is* its fallback.

## 6. What gets sent, and when

Nothing is ever sent in the background. Exactly two actions transmit data:

**Ctrl+Enter in the capture bar** sends: today's date + the one line you
typed. That's all.

**Sending a message in the Assistant** sends: your message, the recent
conversation, and a compact "briefing" the app generates fresh each time —
today's blocks and their tracked time, task titles due soon, special days,
and your life-area names. It deliberately **excludes** task descriptions,
event notes, and all internal ids.

It also sends your **memory file**, if you have written one (Settings →
Memory). That is the short list of lasting things about you — routines,
preferences, your current situation, people — and it goes with **every**
message, not just the first. It is capped at about 1200 characters; anything
past that is left out whole rather than cut off mid-sentence, and the settings
page tells you when that is happening.

Two things about memory worth being exact about, because they are easy to
confuse:

- It **does not sync.** It lives beside your planner as `memory-<you>.md` and
  never travels to the TickTimer server, so it does not follow you to another
  device.
- That is **not** the same as never leaving your machine. It is sent to your
  AI provider on every Assistant turn, exactly like the briefing. If that
  matters for something you were about to write down, use Ollama — or don't
  write it down.

The memory file is plain Markdown. You can open it in any text editor, and
anything in it TickTimer doesn't recognise is kept exactly as you wrote it and
never sent.

Don't take this page's word for it: the Assistant's **"What can it see?"**
button shows the exact briefing text, verbatim, and names the provider it
goes to. Errors you see in the chat (⚠ bubbles) are local — they are never
sent to the model.

With **Ollama**, all of the above stays on your machine.

Your data file, sync, and login are completely separate systems — the AI
provider never sees your account, your history, or your data file.

---

## 7. Overriding the model

The **Model** field (blank = the provider's default) exists because model
names age: providers retire them, and better/cheaper ones appear. If a
provider's default ever starts failing with an HTTP error mentioning the
model, look up the provider's current cheap model and type its exact name
here. Overrides are remembered per provider, like keys.

---

## 8. Error messages, decoded

Every failure names its fix. The full list:

| Message | Meaning | Fix |
|---|---|---|
| `no API key set for X (Settings → AI)` | that provider needs a key and has none | paste one (§3), or switch to Ollama |
| `no server address set for X` | Custom endpoint with an empty Address | fill the Address field |
| `API key rejected — check Settings → AI` | the key is wrong, expired, or for another provider | re-paste; check it matches the selected provider |
| `AI endpoint not found (404) — check the address` | the base URL is wrong or missing a path prefix | fix the Address (§5) |
| `rate limited by the AI service — wait a moment` | too many requests (common on free tiers) | wait a few seconds and retry |
| `couldn't reach the AI service` | no network, or the local server isn't running | check connection / start Ollama / start the server |
| `AI service error (NNN)` | the provider returned an unexpected HTTP status | often a wrong model name — see §7 |
| `AI reply was not JSON` / `had no text content` | the endpoint answered, but not in the expected shape | usually the wrong **Dialect** on a custom endpoint |

The **Test** button in Settings surfaces these same messages inline, so you
can debug your setup without leaving the dialog.

---

## 9. Limits worth knowing

- The Assistant sees **today** plus tasks due in the next 7 days — not your
  whole history.
- Conversations are **not saved**: closing the app (or "New conversation")
  clears the chat. Your planner data is untouched either way.
- It can change **one** thing — where a missed block sits — and only via a
  card you tap. It cannot invent a time: the choices come from the app's own
  search, and a placement that is not on that list is refused. Everything
  else remains a design guarantee, not a limitation of the model.
- One conversation turn costs roughly 1,000–2,500 "tokens" round-trip; at
  current provider prices that is a fraction of a cent, and zero on
  Ollama/Groq-free.
