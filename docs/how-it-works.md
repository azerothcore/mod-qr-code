# How it works

The server cannot inject Lua or define client frames, but it can send text containing UI escape
sequences, and the client renders those. `ChatHandler::SendSysMessage` does not escape `|` in
server-originated strings, so the code is drawn as a grid of inline textures — one text line per QR
row, one `|T…|t` escape per run of same-coloured modules:

```
|TInterface/DialogFrame/UI-DialogBox-Background:14:28:0:0:100:100:45:55:45:55|t
```

**Modules cannot be tinted.** The 3.3.5a client accepts the escape's vertex-colour arguments and
then discards them, so asking for a black `WHITE8X8` just gets you a white square — verified in-game
against a raid-target icon, which renders in its own colours whatever vertex colour it is given. Each
colour therefore has to come from a texture that already is that colour: `Interface/Buttons/WHITE8X8`
for light modules, `Interface/DialogFrame/UI-DialogBox-Background` for dark ones.

The trailing numbers crop the texture to a sub-rect. That matters for the dark one, which is a
patterned stone panel: stretching the whole thing across a module shows the pattern, while a small
flat crop gives an even block, and evenness is what a decoder needs. Both textures and both crops are
config options, since which client textures are usable is not something the server can know.

The interesting part is the seam between rows. The chat font fixes the row spacing at roughly 16 px
and it does not shrink when the modules do, so anything smaller than that leaves a gap on every row.
Every escape in row *N* therefore carries a vertical offset of `-N * (LineAdvance - ModuleHeight)`,
lifting each row to close the gap without shrinking the layout height the text block reserves.

`LineAdvance` reads as a dial rather than a measurement: the client treats a positive offset as
**upward**, so a *lower* value packs the rows tighter, and 0 or negative is the normal setting. At
the default 7 px modules it lifts each row by 7 px, which lands them flush.

The offset moves only what is drawn, never the height each line reserves, so a grid of short modules
always leaves slack — and the smaller the modules, the more of it. `QRCode.AnchorBottom` decides
which end of the grid keeps its natural position: with it on, the last row sits on its own line and
the code lands just above the chat input box with the slack above it, where it reads as ordinary
empty chat. Turn it off and the code floats high in the frame with a conspicuous gap underneath.

That makes modules non-square on the quest backend. Decoding is unaffected: a uniform aspect scale is
an affine transform, which decoders resolve from the three finder patterns.

## Packing rows into one line

`QRCode.RowsPerLine` draws several module rows inside a single escape, cutting the chat lines a code
needs by that factor. Lines, not bytes, are what the chat frame runs out of, so this is the setting
that decides whether a code fits on screen at all.

| rows/line | modH | lines (v1) | on-screen | line grows? |
| --- | --- | --- | --- | --- |
| 1 | 7 | 23 | 368 px | no |
| 2 | 7 | 12 | 192 px | no |
| 3 | 7 | 8 | 168 px | **yes** |
| **3** | **5** | **8** | **128 px** | no ← shipped |

The ceiling is the chat font's fixed line advance, around 16 px. While `RowsPerLine * ModuleHeight`
stays under it the line keeps its normal height; past it the line grows to fit the taller escape and
hands the saving straight back. Two 7 px modules come to 14 px and fit, which is why 2 is free at the
shipped module size. Three only pays if you also drop `ModuleHeight` to about 5 and retune
`LineAdvance` — modules then become 7 wide by 5 tall, which decoders resolve as an affine transform
from the finder patterns, the same reason the quest backend gets away with non-square modules.

A packed escape needs a texture that already contains the whole column, because an escape names
exactly one texture; a dark module and a light one cannot be composited. Scanning the 3.3.5a
interface textures for an opaque rectangle with a hard horizontal black/white edge turns up 80
candidates and only two with flat bands in both directions. **Supply, not arithmetic, is what limits
this.** A candidate also has to be *neutral*, not merely the right brightness: luminance alone lets a
tan pixel through, and RGB 240,236,152 scores about 230 while rendering as a visibly yellow module.
Most of the client's artwork is warm, which rules it out. Two rows needs 4 crops and is comfortable,
three needs 8 and they exist but unevenly, four would need 16 and has no usable set.

Defaults for two rows crop the USK age-rating badge, `Interface/Glues/Login/Glues-GermanRating`,
which carries luminance 0 directly above 248 with 13 px of each. The three-row set scans at the shipped
4×5 modules but is the less comfortable of the two: its darks sit near 32 while `LDL` is 1 and `DLD`
is 26–37, and those two come from bands only 2–3 px tall. They hold up at 5 px and are the first
thing to suspect if a taller module stops scanning. All patterns in use
must agree on their black and their white, or the seam between lines reads as a module edge.

Packing buys far less in bytes than in lines, because a run has to agree on every row it spans and
merging weakens as fast as the line count falls. Measured on `https://www.azerothcore.org`:

|                | lines | escapes | bytes  |
|----------------|------:|--------:|-------:|
| one row a line |    27 |     349 | 20,559 |
| two rows       |    14 |     246 | 15,656 |
