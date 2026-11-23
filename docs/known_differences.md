# Completion audit — updated 15 November 2026

**The complete conversion has not been achieved.** The supplied ROMs are
available and validated. Remaining game systems require substantial further
reverse engineering and implementation; they are not blocked by missing ROMs.

| Requirement / system | Actual status |
|---|---|
| Standard cartridge; H40 320×224; checksum/vectors | Implemented and verified in Genesis Plus GX; independently booted in MAME Genesis |
| Extra horizontal scenery without stretching | Implemented; source camera math + 32-pixel viewport extension; screenshots supplied |
| Original graphics extraction, palette PROMs/LUTs | Implemented and verified by checksums, layout tests and reference scenery matching |
| All original round definitions | 32 decoded from ROM; later layout reuse confirmed in code |
| All 32 native scenery loads | Implemented and verified at entry; later sections not all visually inspected |
| Terrain profiles and ordinary landing | Partially implemented from original height tables; roof, wall and special collision logic incomplete |
| Movement, tapping/holding, jumping | Partial translation of isolated original routines; complete player state machine and differential gameplay verification pending |
| Native animation, original/corrected width options | Implemented and visually inspected; animation timing only approximate; no pixel cleanup |
| Original enemy types and behaviour | Implemented by running the original object code recompiled to C (`docs/recompilation.md`); translation byte-identical to a reference 6809 interpreter in all 32 rounds; behaviour checked visually in sample rounds only (no frame-by-frame MAME comparison yet) |
| Original activation/spawn/despawn and difficulty | Original code (gates 82C0, spawner 85D5, set-up 8010/80DA); viewport width does not affect it |
| Vehicles, moving platforms, falling ghosts, chase logic | Objects run (recompiled); standing on platforms/logs needs the arcade player routine 8731, which is not run: native Pac-Man falls through |
| Score, extra lives, time bonus, collectibles and secrets | Not implemented; development lives exist, arcade scoring does not |
| Power-ups, helmet, key, invisibility and warps | Not implemented |
| Full boot/return-trip mechanics | Partial repeated jump only; other original interactions pending |
| Round advancement | Development coordinate transition exists; original completion scripts absent |
| Queen/fairy/family scenes, trip endings and attract mode | Not implemented |
| Post-round-32 layout reuse | Development fixture verified 32→17; authentic transition sequence and difficulty escalation absent |
| Lives, pit death, respawn, game over and restart | Implemented development shell; injected-state tests passed; checkpoints/continue rules not arcade-faithful |
| Music | Partial: 11.88-second original MAME music excerpt, 13.3kHz signed PCM via XGM2 DAC; cut is not a verified musical loop |
| Event sound effects and full soundtrack | Not implemented |
| Palette adaptation | Implemented with quantization; material colour differences remain, especially complex later scenery |
| Per-pixel priority/cookie-cut lantern effects | Not implemented; ordinary VDP priority only |
| Native-refresh performance | Low measured DMA in a player-only scene; full-game CPU/VRAM/sprite stress performance unverified |
| PAL timing compensation | Implemented but unverified |
| Physical Mega Drive/flash cartridge | Designed within ordinary limits, unverified on hardware |
| Full round completion / full game ending | Not achieved or verified for any round |

The sampled music has measurable nominal-rate drift. A three-second native
recording correlates with the source excerpt after explicit rate/phase
alignment; `dist/audio-validation.json` retains both unadjusted and adjusted
results. This is evidence of the intended sample playing, not a claim of
arcade sound-hardware equivalence or perceptual audio approval.

Development round selection is intentionally present because this is not a
release build. Its access to all 32 layouts must not be confused with a full
length populated game. Repeated or invented placeholder stages were not used.

## Intentional pool springboard assistance

Pool takeoff state 6 receives a one-time extra `0x0080` of upward velocity (half a pixel per simulation tick), with flying boots off. This intentionally increases launch height; maps, ordinary jumps and original directional tapping remain unchanged. In the final Round 6 pool replay, peak Y rises from 156 to 177 and the same rightward tap cadence reaches the bank instead of falling seven pixels short. These are controlled approach/controller fixtures, not complete playthroughs.
