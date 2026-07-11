# TickTimer — how to run it (start here)

Hi! You're testing TickTimer. It's **two programs** that work together, and
you just need to start them in the right order. No installing anything.

## The one rule

**Start the server first, then the app.** The app can't log in until the
server is running — like an online game needing its server up before you can
join.

---

## If you're running BOTH on this one computer

1. Double-click **`Start TickTimer server`**.
   A black window opens and stays open. That's normal — **leave it open.**
   It will say something like `listening on port 8080`.

2. Double-click **`TickTimer`**.
   A login screen appears. The **Server** box already says
   `http://localhost:8080` — that's correct for this computer. Leave it.

3. Click **"New here? Create an account"**, pick a username and password,
   and you're in.

When you're done, close the app, then close the black server window.

---

## If the server is on a DIFFERENT computer (e.g. your boyfriend's PC)

You only run the **app** (`TickTimer`), not the server. On the login screen,
change the **Server** box to the address his server window printed — it
looks like:

```
http://192.168.1.20:8080
```

(He can read that address off his own server window — the line that says
"from another device".) You both need to be on the **same Wi-Fi**.

Type it in, create your account, log in. Done.

---

## Things that might go wrong

- **"Can't reach the server."** The server isn't running, or the Server
  address is wrong. Check the black window is open; check the address.
- **Windows SmartScreen warning** ("Windows protected your PC"). Because
  this isn't a signed commercial app, Windows is cautious. Click
  **More info → Run anyway**. It's safe — it's just us.
- **The black window flashes and vanishes.** Something stopped the server
  immediately (usually: it's already running in another window). Close any
  other server windows and try again.

## Share & compare (the fun part)

Once you're both logged in against the same server: in the app, click the
**👥 Share** button, type the other person's username, and hit Share. Then
they'll see a **Compare** button next to your name — click it to see both
your days side by side: the time totals AND each person's schedule for the
day (handy for spotting when you're both free). Sharing is one-way, so you each share with the other
if you both want to see.

Thanks for testing! 💜
