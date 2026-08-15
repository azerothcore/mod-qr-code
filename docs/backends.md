# Backends

**Chat (1)** is the default and the safer of the two. The frame is resizable by the player, so there
is no width to fit inside; modules are square and the row offset collapses to zero, which means no
seams and no aspect distortion.

**Quest (2)** opens a quest details frame carrying the code as the quest description. The pane is
roughly 285 px wide and scrolls vertically, so modules have to be narrow and non-square. The frame is
server-pushed and its strings are inline, so nothing is written to the world DB or the client's quest
cache. Accept and Decline both close it cleanly.

**The gossip menu** is not a backend but the `.qr gossip` command: it opens a gossip window with a
"Show the QR code" option, and picking it prints the code in the chat frame, so the chat geometry
applies. The menu is only an entry point — the gossip window cannot carry the code itself, because
the client stops opening the window once its body text passes a limit somewhere between 3 and 4 KB
(measured in-game with `.qr grid`), and even a minimum-size code needs several times that. The quest
frame is no alternative hand-off target either: a full-size code in its description string crashes
the client (see Known limitations). The menu's body text travels as an `npc_text` id: the module
pushes the greeting as an unsolicited `SMSG_NPC_TEXT_UPDATE` under a reserved id (16777200 — just
below the core's default greeting id 16777215, and above the rest of the world DB, which stops at
921061), which the client caches on receipt. The window's gossip source is the player themself, so
no NPC has to exist or be targeted.
