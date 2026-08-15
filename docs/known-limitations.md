# Known limitations

- **Chat is the only surface known to hold a code, and not because its buffer is bigger.** Every
  other frame takes the whole grid as one string, and no string surface measured so far holds more
  than about 8 KB, against 31 KB for the cheapest full code. Chat wins by sending 35 separate ~1 KB
  messages. Ceilings below were measured by rendering checkerboards of known size into each surface
  and finding where the frame goes blank:

  | Surface | Ceiling | Fits a code? | Evidence |
  | --- | --- | --- | --- |
  | Chat | ~1 KB per line, 35 lines | yes | the only one that splits the grid across many strings |
  | Page text | 32,766 B (one packet per page) | unmeasured | `SMSG_PAGE_TEXT_QUERY_RESPONSE` carries one page alone |
  | GM ticket response | 3,999 B | no | `GmTicket::SendResponse` sends 4 chunks of 3,999 but the client renders only the first — byte 3,999 shows up on screen as raw `\|T` text |
  | Mail body | 7.3-8.3 KB | no | 7,258 B renders, 8,322 B is blank — matches the `max 8000` note in `MailHandler.cpp` |
  | Quest text | 2.9-5.9 KB | no | 7×7 and 10×5 render, 10×10 is blank |
  | Gossip body | 3.4-4.4 KB | no | 7×7 opens the window, 8×8 does not |
  | Calendar event | 255 B | no | `varchar(255)` column, and `CalendarHandler.cpp:248` rejects longer |

  Oversized text fails silently and blank, never with an error. Mail has a second cap on top: the
  server drops any mail that would push `SMSG_MAIL_LIST_RESULT` past `MAX_NETCLIENT_PACKET_SIZE`
  (32,766), which the client reports as "your inbox is full" — and because every mail in the inbox
  shares that one packet, a large body can push the others out of the list.

  For scale: the smallest QR that exists — version 1, 21 modules, at three rows per line with the
  quiet zone dropped entirely — is 8,322 bytes, which mail renders as blank. Nothing is left to
  trim, so no surface but chat can hold a code. That is why the gossip
  backend is a menu handing off to chat rather than a display surface of its own, and why no mail,
  quest or ticket backend exists.
- **The quest backend cannot show a full-size code, and can crash the client trying.** A full-size
  code in the quest description crashed the 3.3.5 client outright, and the ceiling above puts a real
  code an order of magnitude past what quest text holds anyway. It is kept for small payloads and
  calibration only: start from small `.qr grid` sizes with `QRCode.Backend = 2`.
- **Chat timestamps break the chat backend.** A player with timestamps enabled gets a prefix on every
  line, which shifts each QR row sideways relative to the last. This is a client setting; the server
  cannot override it. Turn timestamps off before scanning.
- **A code is large.** A minimum-size symbol runs to roughly 15 KB of escape sequences, a version 3
  one to 27 KB and a version 4 one to 37 KB, at about 1 KB per chat line. That is why the cooldown
  exists and why `MaxPayloadBytes` defaults high — a 12000 byte cap would reject every code.
  Texture paths are charged on every run, so they are worth real bytes: dropping
  `QRCode.DarkTexCoords` and using the shorter `Interface/Tooltips/UI-Tooltip-Background` takes the
  same version 4 code from 38,829 to 31,104 bytes, at the cost of a greyer dark module.
  `QRCode.RowsPerLine` cuts the line count, but it is not a way under a byte ceiling: measured
  against these cheapest paths it saves 16% on a version 1 code and only 5% on a version 4 one,
  because a packed style has to carry a longer path and a crop on three of its four states.
- **Capacity is small.** Geometry, not the encoder, is the binding constraint: version 5 at ECC L
  holds 106 bytes, version 3 only 53. Long URLs need shortening before they will fit.
