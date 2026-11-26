# nativemd — plan update (2026-11-28)

Supersedes `ROADMAP/2026-11-26/update.md` and its handoff. History:
`RECORD/2026-11-28.vdp-render.WIP.md`.

## Changed

The VDP renderer (milestone 4) moves ahead of the remaining hardware-fact test ROMs: the
Sonic 1 client needs pictures to show its progress (the C already produces the right VDP
state, compared every frame; only the step from state to pixels was missing).

## Done since the last update

- Renderer, first version (`src/render.c`): mode 5, planes A / B with full / cell / line
  horizontal scroll and full / two-cell vertical scroll, the window, sprites (link list,
  20 / 16 per line, 320 / 256 dots per line, masking by X = 0), priorities, backdrop, left
  column blank, display off. Drawn line by line from `vdp_line`, before the line's HINT, in
  the engine and in the oracle. `nmd_framebuffer()`; `nmd_config.no_render` for headless
  runs. `test_render` (23 tests in all).
- `--screenshot N:FILE.png` on the hybrid runners (PNG writer without dependencies).
- Sonic 1 (client): Sega and title screens, GHZ, LZ (the HBlank water palette), SBZ draw
  as expected; with C and without C the pictures are identical.

## Next

1. **Renderer, rest of mode 5**: shadow / highlight, interlace (mode 2), the partial first
   column of two-cell vertical scroll, sprite collision / overflow status bits, the window
   bug with horizontal scroll; checked on own test ROMs (one per feature, picture hashes).
2. **A desktop host** (window, keyboard / pad, frame pacing) so a game runs interactively.
3. **Hardware-fact test ROMs** for the VDP (+ MiSTer runs), as before.
4. **Z80 coroutine** (5), **PSG + YM2612** (6).

```mermaid
gantt
    title nativemd plan (update, 2026-11-28)
    dateFormat YYYY-MM-DD
    section Done
    VDP renderer, first version + screenshots :done, d1, 2026-11-28, 1d
    section Engine
    Renderer: rest of mode 5, test ROMs       :e1, 2026-11-29, 7d
    Desktop host (window, input)              :e2, after e1, 5d
    Hardware-fact test ROMs (+ MiSTer)        :e3, after e2, 10d
    Z80 coroutine                             :e4, after e3, 10d
    PSG + YM2612                              :e5, after e4, 30d
```
