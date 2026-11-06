# AUDIO PLAYER - steel concept, orange / white (print-ready, no supports)
Body 180 x 80 x 40 mm (end ribs +2 mm/side, top rails +3 mm). All earlier measurements kept: screen opening 69.40 x 50.15, 2 x 40 mm speakers with equal spacing, 3 keys on top.

## Parts (all STL are oriented for printing)
| STL | Colour | Qty | Orientation / note |
|---|---|---|---|
| steel_front.stl | orange | 1 | front face down |
| steel_overlay.stl | white | 1 | face plate 1.6 mm, glued on the front plate |
| steel_trim.stl | orange | 1 | screen bezel + 2 speaker rings + nameplate base (1.2 mm), glued on the white plate |
| steel_letters.stl | white | 1 | "ESP AUDIO PLAYER" letters 0.8 mm, glued on the nameplate (or colour-change at 1.2 mm if you print letters with trim) |
| steel_letter_jig.stl | any (light grey) | 1 | placement jig for the letters: lay over the nameplate, drop letters into the windows, wick in thin CA, lift off |
| steel_heads_small.stl | silver/white | 10 pcs | small round bolt heads 3.6 mm (4 around each speaker ring + 2 on the nameplate), dome UP, glued on |
| steel_heads_big.stl | silver/white | 8 pcs | big round bolt heads 5.7 mm for the 8 front corner positions - optional printed look-alike instead of the real M3 button-head screws (glue on; the case is then closed another way) |
| steel_shell.stl | orange | 1 | back face down |
| steel_bracket.stl | any | 1 | as is |
| steel_keys.stl | white | 3 keys | as is |

## Hardware
- Battery: 55.05 x 67.38 x 9.19 mm LiPo pouch cell including its wire (wire leaves on a short edge), lying with the 67.38 mm side along X. The USB-C / microSD ports were moved 3 mm down (PORT_Y = 10) to make room.
- 8 x M3x12 button-head socket screws (ISO 7380, the "round head allen" bolts) + 8 x M3 heat-set inserts. Screws sit on the white plate, none above the screen.
- 3 tact switches 6x6x5, 2 x M2x8, 2 x 40 mm speakers, 3.7 V 2000 mAh LiPo, 4 rubber feet.
- USB-C extension: slot 10.0 x 3.8, ear holes 3.2 mm at 17 mm; 2 x M3x6 screws + 2 x M3 nuts (captive hex pockets on the inside pad).
- microSD extension: slot 19.5 x 3.1, no screw holes; the extension rests on the support shelf (top flush with the lower edge of the slot) - fix it with a drop of glue or tape.
- The USB-C flange sits on a 1.5 mm pad on the inside of the back wall; screws go in from the inside into the captured nuts.

## Print (Bambu P1S, PLA, 0.20 mm, 4 walls, 15-20 % gyroid, supports OFF)
Everything is self-supporting: raised details are separate parts, bosses are full-height posts, ribs run to the bed, key holes are round (short 7.4 / 10 mm bridges at the top arc).
Only short bridges remain (<= 9 mm): the 2 mm port recesses on the back and the tact-switch pockets in the bracket.

## Assembly
Glue white plate on front plate, trim on the white plate, letters on the nameplate, heads on rings/nameplate. Insert heat-set inserts in the shell posts, assemble as before (bracket, keys, display, speakers, battery), close with 8 x M3x12.

## microSD support shelf (from your sketch)
A 19.5 x 18 mm block stands on the bottom wall against the back wall. The flat extension cable lies on top of it; the top of the block is flush with the lower edge of the slot. Parameters: SD_SUP_W / SD_SUP_L in the .scad.

## Alignment pins
- White face plate has 4 locating pins (2.4 mm) on its back that drop into 4 blind holes in the front plate.
- Orange trim has 8 pins on its back (2 on the bezel, 2 per speaker ring, 2 on the nameplate) that drop into matching holes in the white plate. Hole = pin + 0.4 mm clearance.
- Print the trim and white plate face down; the pins point up, no supports.

## Foot recesses
4 rubber-foot recesses (10 mm) on the bottom of the shell, symmetric: x = 25 / 155, z = 11 / 33 from the front.

## Speaker mount (SQ-157E 1.5" / 40 mm, 4 ohm)
- Front rim pocket 42.6 mm (rim ~42 mm, estimated from the product photo), rim rests on the 2 mm lip behind the grille.
- 2 screw posts per speaker, 49 mm apart, ears pointing UP/DOWN (sideways the 55 mm ears would hit the case wall).
- Post top = ear plane, 6 mm behind the speaker front. Pilot holes 2.2 mm: 4 x M2.5 x 6 self-tapping screws (2 per speaker).
- Speaker depth 20 mm fits easily (back of magnet ends ~22 mm from the front, shell interior is 36 mm deep).
- Measure your speaker rim with calipers and change SPK_RIM_D if it is not 42 mm.

## Colourways
Set COLORWAY in audioplayer_steel.scad: "orange_white" (original), "black_orange", "purple_orange".
Body colour = shell, front plate, trim (bezel/rings/nameplate base). Accent colour = face plate, letters, keys.
Previews: img/cw_<colourway>_3q.png and img/cw_<colourway>_flat.png. STLs are the same for all colourways, only the filament changes.
