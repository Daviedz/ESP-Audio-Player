# Print guide - Bambu Lab P1S, 0.4 mm nozzle, PLA only

Ready-to-print files, already sliced in Bambu Studio. Copy the `.gcode.3mf` files to the printer's SD card
(any folder), then choose a file on the printer screen and start it. No slicer needed. One filament per plate, no AMS needed.

| # | File | Filament | Layer | Time | Filament | Contents |
|---|---|---|---|---|---|---|
| 1 | `1_shell_PLA_orange` | Orange PLA | 0.20 | 3 h 37 min | 114 g | Rear shell (3 mm outer brim) |
| 2 | `2_bracket_PLA` | Any PLA | 0.16 | 15 min | 4.5 g | Key switch bracket |
| 3 | `3_front_plate_PLA_orange` | Orange PLA | 0.20 | 1 h 03 min | 30 g | Front plate |
| 4 | `4_white_face_plate_PLA_white` | White PLA | 0.20 | 47 min | 16.5 g | White face plate (1.6 mm) |
| 5 | `5_trim_PLA_orange` | Orange PLA | 0.12 | 10 min | 3 g | Bezel, speaker rings, nameplate base |
| 6 | `6_small_parts_PLA_white` | White PLA (silver for heads if you like) | 0.12 | 14 min | 2 g | Letters, letter jig, 3 key caps, 10 small + 8 big bolt heads |

Total about 6 h 10 min and 170 g. Print order: 1 (shell, checks the ports) -> 3 -> 4 -> 5 -> 6 -> 2.
Colours follow the orange/white colourway; for black/orange or purple/orange just load those filaments instead.

## Settings used (from `P1S_PLA_PETG_print_settings.xlsx`, PLA column)
- Printer: Bambu Lab P1S 0.4 nozzle. Build plate: Textured PEI (or Cool Plate). Door open or top lid off (PLA heat creep).
- Nozzle 220 C, bed 55 C, part fan 100 %, aux fan 100 %, max volumetric speed 18 mm3/s.
- Walls 4, top/bottom layers 5/4, infill gyroid 20 % (shell, front plate); thin parts use ~solid infill (99 %).
- Support OFF, ironing OFF, arc fitting ON, detect thin walls ON, seam aligned, elephant-foot compensation 0.1 mm, z-hop 0.4 mm, retraction 0.8 mm.
- Outer wall 120 mm/s, inner wall 200, infill 250, bridges 40 mm/s. Plate 6 first layer 40 mm/s (thin letters).
- Based on the stock "Standard @BBL X1C / Fine @BBL X1C" process and "Bambu PLA Basic" filament profiles; weights assume PLA at 1.24 g/cm3.

## Notes
- Make sure the filament loaded matches the plate; the files use the PLA profile whatever brand you load. Check the spool label for the temperature.
- The 100 % infill is set to 99 % because Bambu Studio's command line refuses exactly 100 % with these profiles; the result is the same for these thin parts.
- Assembly and hardware are in `../README.md`.
