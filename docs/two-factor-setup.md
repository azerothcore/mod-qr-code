# Two-factor setup by QR

A two-factor payload needs a version 4 symbol — 35 module rows. At the shipped `QRCode.RowsPerLine` 3, that is
12 chat lines and fits a default frame; with it off it is 35, which does not, and the player gets the
bottom two thirds of a code and no way to scan it. The command therefore ships enabled alongside
packing. **Turn it off if you set `RowsPerLine` to 1**, or confirm `.qr grid 35` fits your frame first —
see the chat-lines section below for what buys the room. When disabled the command refuses and points
the player at `.account 2fa setup`, which is the working fallback.

The core's `.account 2fa setup` prints a 32-character Base32 key and asks the player to type it into
an authenticator app before confirming with a token. `.account 2fa qrcode` is the same two steps with
the typing removed:

```
.account 2fa qrcode          -- draws the key as a code; scan it
.account 2fa qrcode 123456   -- the token your app now shows; this is what enables 2FA
```

**Nothing is written to the account until the token checks out.** A key stored on the draw alone
would lock the player out at the next login whenever the scan silently failed — recoverable only by a
GM running `.account set 2fa <account> off`. The key is printed as text under the code too, for
authenticator apps that are easier to type into than to point at a screen. `.account 2fa remove
<token>` is unaffected and still works.

Redrawing hands out the same key until it is confirmed, so a player who scanned the first code and
then asked for it again is not left holding an entry no token can complete. Pending keys live in
memory only and are dropped on restart; draw again after one.

**It replaces `.account 2fa setup`, it does not extend it.** The core keeps the key that command
offers in a `static` local to its own handler, so no module can render *that* key; the one here is a
second, independent secret, and its token only completes this command. Both write the same
`account.totp_secret` column and neither runs once it is set, so the two flows cannot collide — a
player takes one or the other. Admins who want only the QR flow visible can raise `account 2fa setup`
in the `command` table.

Once two-factor is enabled the command refuses, because re-showing a live secret would turn any
unattended session into a cloned authenticator.

The payload is an [otpauth URI](https://github.com/google/google-authenticator/wiki/Key-Uri-Format)
carrying only the label and the secret. AzerothCore's TOTP parameters — 6 digits, SHA1, 30 seconds —
are the format's defaults and are left out, as is the redundant `issuer` query parameter, purely to
keep the byte count down.

**It needs room.** Even a two-character account name with no issuer at all comes to 57 bytes, past
the 53 a version 3 code holds, and a realm name plus a long account name reaches version 5:

| Label | URI bytes | Version | Modules | Rendered | Row width at 7 px |
| --- | --- | --- | --- | --- | --- |
| `AzerothCore:HELIAS` | 73 | 4 | 33 | 36 KB | 245 px |
| `My Test Realm:AVERYLONGACCOUNT` | 89 | 5 | 37 | 46 KB | 273 px |

The row is wider than the symbol because of the quiet zone on each side.

The shipped `QRCode.MaxVersion = 5` and `QRCode.MaxPayloadBytes = 48000` cover both rows. Both are
ceilings rather than sizes — every payload still renders at the smallest version that holds it — so
if you have lowered either from an earlier release, raise them back or the command fails with a
config error instead of drawing anything.

**The binding constraint is chat lines, not pixels.** One QR row is one chat line, and the whole code
has to be on screen at once — a version 4 symbol is 33 modules plus the quiet zone on each side, so
35 lines at the shipped `QRCode.QuietZone = 1`, and anything past the top of the frame is simply not
there to scan. Shrinking `ModuleHeight` does not help: a chat line reserves the full font line height
whatever is drawn in it. What does help is a smaller chat font (right-click the chat tab → Font Size)
and a taller chat frame. `.qr grid 35` tells you in one command whether 35 lines fit.

The quiet zone is the reason the default is 1 rather than the 4 the spec asks for: each module of
border is two more lines to find room for. Raise it if a code will not scan.

Note that `LineAdvance` is tuned against the font's line spacing, so changing the font size means
retuning it: the flush value is `2 × ModuleHeight − line height`, and `.qr grid` shows whether you
got it.

Also worth having is a chat frame wide enough for the row: a wrapped row destroys the grid.
`QRCode.TwoFA.Issuer` shortens the label if the realm name is long, and every byte saved there is
worth having, since crossing a version boundary costs four more rows. The quest backend is no help
here either: at `QuietZone = 1` a version 4 code is 245 px wide and does fit the 285 px pane, but the
pane is only about 300 px tall against the ~490 px those 35 lines need, and it is the backend that
has crashed the client at full size.

The secret is drawn into the chat frame in the clear, exactly as `.account 2fa setup` prints it in the
clear. Anyone watching the screen or the stream can enrol the same authenticator, so it is worth
telling players to run this alone.
