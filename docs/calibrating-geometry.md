# Calibrating the geometry

The three pixel values interact, and "it scanned or it didn't" is not enough feedback to tune them
against. Work in this order:

1. Set `QRCode.CooldownSeconds = 0` and `.reload config`.
2. `.qr grid`. The squares must alternate clearly dark and light — if they all look the same, the
   two textures are not distinct enough and `QRCode.DarkTexture` needs changing. Preview any
   candidate in-game without touching the server:

   ```
   /run P=string.char(124) DEFAULT_CHAT_FRAME:AddMessage(P.."TInterface/Buttons/WHITE8X8:24:24"..P.."t")
   ```

3. Still on `.qr grid`, adjust `LineAdvance` and `.reload config` until the rows touch without
   overlapping. **Lower is tighter** — gaps left between rows means go down (negative is fine),
   rows overlapping or squashing means go up. Retune after any change to `ModuleHeight`.
4. `.qr probe 200`, then binary-search `n` upward until the three dark squares at the end of the
   line stop appearing. That is the client's real inbound line cap; set `QRCode.MaxPayloadBytes`
   comfortably below it.
5. `.qr https://example.com` and scan it with a phone.
6. Put `QRCode.CooldownSeconds` back.

If a code with clean geometry still refuses to scan, raise `QRCode.ErrorCorrection` from `L` to `M`
before changing anything else.
