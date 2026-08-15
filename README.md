# mod-qr-code

Renders a scannable QR code to a player on a completely unmodified 3.3.5a client. No addon, no MPQ
patch, no client-side files of any kind.

```
.qr https://www.azerothcore.org
```

![A QR code drawn in the chat frame of an unmodified 3.3.5a client](images/qr-code-in-chat-fullheight.png)

## Documentation

- [How it works](docs/how-it-works.md)
- [Two-factor setup by QR](docs/two-factor-setup.md)
- [Colouring QR Code](docs/colouring.md)
- [Backends (chat and quest frame)](docs/backends.md)
- [Calibrating the geometry](docs/calibrating-geometry.md)
- [Known limitations](docs/known-limitations.md)

## Requirements

None beyond AzerothCore itself. The QR generator is vendored, so there is no new dependency, and the
only SQL shipped is one `command` table row, for `.account 2fa qrcode`.

## Installation

```bash
cd modules
git clone https://github.com/azerothcore/mod-qr-code
cd ..
# reconfigure and rebuild the core as usual
```

Then edit `qrcode.conf` in your config directory (the build copies `conf/qrcode.conf.dist` there).

## Commands

| Command | Level | Description |
| --- | --- | --- |
| `.qr <text>` | Game Master | Renders `<text>` as a QR code via the configured backend. |
| `.qr gossip <text>` | Game Master | Opens a gossip menu whose "Show the QR code" option presents the code, whatever the backend. |
| `.qr say <text>` | Game Master | Says the code aloud, so everyone in say range can scan it off their own screen. |
| `.qr color [name] [text]` | Game Master | Renders in one of the realm's configured colours. With no name, lists them. |
| `.qr swatch <texture> [texCoords]` | Game Master | Draws a candidate texture three ways so you can judge it as a module colour. |
| `.qr sweep <texture> [cells]` | Game Master | Samples a texture on a grid, drawing one flat bar per point with its texCoords beside it. |
| `.qr grid [rows] [cols]` | Game Master | Checkerboard at the active geometry, no QR encoding. Defaults to 10×10. |
| `.qr probe <n>` | Game Master | Draws one line of `n` alternating modules ending in a three-module black sentinel. |
| `.account 2fa qrcode [token]` | Player | Draws a new two-factor key as a code to scan; run again with a token to enable it. Turn off with `QRCode.TwoFA.Enable`. |

All of them need an in-game session; none work from the console. Both sides of `grid` are optional —
a single argument means a square of that size.

### Saying a code to the room

`.qr say <text>` sends the grid as the GM's own say chat instead of as a system message, one say per
line, so every client within `ListenRange.Say` (40 yards by default) draws the code in its own chat
frame. It is the way to put a scannable link in front of a crowd — an event sign-up, a Discord
invite — without every player having to run a command.

The rows always render at the chat geometry, whatever `QRCode.Backend` says, since chat is where they
land. Everything the chat backend needs still applies to every viewer individually: a chat frame tall
enough for the whole code, wide enough not to wrap a row, and timestamps off.

Each line arrives behind the client's `<name> says:` prefix. That prefix is the same width on every
row, so it shifts the grid sideways as a block rather than shearing it — which is exactly why
timestamps break the code and this does not. The say is sent as the universal language, because the
client garbles say text in a language the reading character does not know, and a garbled row is a
destroyed row.

Two things to be aware of before using it in a busy place. Viewers with chat bubbles enabled get one
bubble per line, each carrying a couple of kilobytes of escape sequences — the bubbles are not a
surface this module has calibrated, and they are the first thing to blame if a client behaves oddly.
And a code is tens of kilobytes sent to every player in range, not just to the caller, so leave the
command at Game Master and leave `QRCode.CooldownSeconds` alone.

### Colouring a code

`.qr color <name> <text>` draws a code in a named colour, and `.qr color` on its own lists what is
available. They ship compiled in, so they work with no configuration at all.

| red | green | blue |
| :-: | :-: | :-: |
| ![Red QR code](images/qr-code-red.png) | ![Green QR code](images/qr-code-green.png) | ![Blue QR code](images/qr-code-blue.png) |
| `.qr color red <text>` | `.qr color green <text>` | `.qr color blue <text>` |

| yellow | purple |
| :-: | :-: |
| ![Yellow QR code](images/qr-code-yellow.png) | ![Purple QR code](images/qr-code-purple.png) |
| `.qr color yellow <text>` | `.qr color purple <text>` |

Yellow is black modules on a gold ground rather than gold modules: gold is too light to threshold
against white.

A coloured code is about three times taller than the same payload in black and white, because no
client texture can pack several module rows into one chat line in colour.

Adding your own colour, picking a texture, and the luminance rule behind all of it:
[docs/colouring.md](docs/colouring.md).

### Opening `.qr` to regular players

The world DB's `command` table overrides the compile-time level at load, so no rebuild is needed:

```sql
DELETE FROM `command` WHERE `name` = 'qr';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('qr', 0, 'Syntax: .qr $text\r\nRenders $text as a scannable QR code.');
```

Reload with `.reload command`. Leave `grid`, `probe`, `swatch` and `sweep` at Game Master — they are
calibration tools, and `probe` exists specifically to send lines large enough to find the client's
limits. Leave `say` there too: it is the one command that ships its output to other people's clients.

The per-player cooldown and the payload echo's escape sanitisation are always compiled in, precisely
so that opening the command up is a database change rather than a rebuild.

## Configuration

Every option lives in `qrcode.conf` and is documented inline there. The ones worth knowing about
before first use:

| Option | Default | Purpose |
| --- | --- | --- |
| `QRCode.Backend` | 1 | 1 = system chat, 2 = quest details frame |
| `QRCode.MaxVersion` | 5 | Caps QR size. The quest frame cannot show past 3; chat past 5 is unwieldy |
| `QRCode.MaxPayloadBytes` | 48000 | Hard cap on the generated string |
| `QRCode.CooldownSeconds` | 5 | Per-player rate limit. Set to 0 while calibrating |
| `QRCode.QuietZone` | 1 | Light border in modules, 1-4. Each one costs two chat lines; the spec asks for 4 |
| `QRCode.RowsPerLine` | 3 | Module rows per chat line, 1-4. Cuts the height a code needs |
| `QRCode.TwoFA.Enable` | 1 | Offers `.account 2fa qrcode`. Turn off if you set `RowsPerLine` to 1 |
| `QRCode.TwoFA.Issuer` | "" | Label shown by the authenticator app; empty means the realm name |
| `QRCode.DarkTexture` / `QRCode.LightTexture` | see conf | Texture per module colour — tinting is impossible |
| `QRCode.Palette.<name>.DarkTexture` | see conf | Overrides or adds a `.qr color` colour |
| `QRCode.DarkTexCoords` / `QRCode.LightTexCoords` | see conf | Sub-rect crop, `texW:texH:left:right:top:bottom` |
| `QRCode.Chat.ModuleWidth` / `.ModuleHeight` | 4 / 5 | Module size. Paired with `RowsPerLine`; see [calibrating](docs/calibrating-geometry.md) |
| `QRCode.Chat.LineAdvance` | -4 | Row-offset dial; lower packs lines tighter |
| `QRCode.AnchorBottom` | 1 | Sit the code at the bottom of its lines, not the top |

Every pixel value is a config option rather than a constant, so the whole module retunes with
`.reload config` — no restart, no rebuild.

**Turning the code is not a way out of the height problem**, tempting as it looks when the frame is
wider than it is tall. A QR symbol is square, so a quarter turn gives back a square of the same side:
the same 35 rows, the same 35 chat lines. Off-axis angles are worse than useless — a 45° turn needs a
bounding box √2 larger, taking 35 rows to about 50, and the `|T` escape places an axis-aligned quad
anyway, so the turned modules would have to be rasterised back onto square blocks, staircase edges
being exactly what a scanner fails on.

### Small size QR code recommended

Compact modules keep the code small and unobtrusive. The trade-off is blank space above the code: a
chat line always reserves the full font line height (~12 px at the default chat font) however short
the drawn modules are, so every row leaves `lineheight − ModuleHeight` px of unfillable slack, which
piles up as empty chat above the code.

```ini
QRCode.Chat.ModuleWidth = 4
QRCode.Chat.ModuleHeight = 4
QRCode.Chat.LineAdvance = -8
```

![A compact QR code drawn in the chat frame](images/qr-code-in-chat.png)

### Max size QR code recommended

Making each module as tall as the chat line itself eliminates the blank space entirely —
`LineAdvance = ModuleHeight` collapses the per-row offset to zero and every line is fully filled.
The code comes out ~300 px wide, so the chat frame must be wide enough: a wrapped row destroys the
grid. If thin seams appear between rows, the real line spacing is slightly larger than the module:
lower `LineAdvance` by 1; if rows overlap, raise it. `ModuleWidth` only affects width — the blank
space depends on `ModuleHeight` alone.

```ini
QRCode.Chat.ModuleWidth = 12
QRCode.Chat.ModuleHeight = 12
QRCode.Chat.LineAdvance = 12
```

![A full-height QR code drawn in the chat frame](images/qr-code-in-chat-fullheight.png)

## Tests

The encoder wrapper, the renderer and the otpauth URI builder are pure functions with no server,
session or client dependency, and `mod-qr-code.cmake` registers their tests with the core's
`unit_tests` target:

```bash
cmake --build build --target unit_tests
./build/src/test/unit_tests --gtest_filter='Qr*'
```

This requires the module to be built statically (the default), since `unit_tests` links the `modules`
static library.

## Credits

QR symbol generation is [Nayuki's QR Code generator](https://www.nayuki.io/page/qr-code-generator-library),
C++ edition, vendored unmodified under `src/vendor/` and used under the MIT licence.

## Licence

AGPL-3.0, matching AzerothCore. See `LICENSE`.
