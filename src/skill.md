---
name: dilute
description: |
  Do lab solution math with dilute: C1V1 = C2V2 dilutions, molarity (mass,
  moles, concentration and volume from a molecular weight), serial dilution
  plans, buffer recipes scaled to a volume, and unit conversion. Use for any
  request to work out how much stock, solid, or diluent to use, to plan a
  dilution series, to make or scale a buffer (PBS, TAE, TBE, TE, Tris-HCl,
  RIPA or a .dilute recipe), or to convert between M, mg/mL, %(w/v) and X.
---

# dilute

No command needs a display. Each takes `--json`, exits 0 on success and 1 on
failure (bad or incompatible units included), and writes errors to stderr.
Warnings, such as a volume too small to pipette, go to stderr in text mode
and into `"warnings"` with `--json`; always pass them on.

Quantities are a number glued to or spaced from a unit: `10mM`, `50 mL`,
`100uL`, `5%w/v`, `70%v/v`, `10X`, `2mg/mL`, `50ng/uL`. Units: M, mM, µM, nM,
pM; g/L, mg/mL, µg/mL, ng/µL, %(w/v); %(v/v); X; g, mg, µg, ng; mol to pmol;
L, mL, µL. A bare `%` is rejected; say w/v or v/v. Molar and mass
concentrations only meet through a molecular weight, and the command says so
rather than guess.

## Commands

- `dilute c1v1 --c1 10mM --c2 1mM --v2 50mL --json` gives three of `--c1`,
  `--v1`, `--c2`, `--v2` and gets the fourth plus the diluent. `--in µL`
  picks the answer's unit.
- `dilute mass --mw 58.44 --conc 150mM --vol 1L --json` solves molarity:
  give all but one of `--mass`, `--conc`, `--vol` (or `--moles` instead of
  `--mass`). With `--mass` and `--moles` it works out the molecular weight.
- `dilute serial --start 1mM --factor 10 --steps 6 --vol 100uL --json` plans
  a series where each tube gets `--vol` from the one before plus diluent.
- `dilute recipe <file.dilute|preset> --vol 500mL --json` works out what to
  weigh and pipette. `dilute recipe --list` names the presets. `--save
  out.dilute` writes the scaled recipe.
- `dilute convert 150mM --to mg/mL --mw 58.44` converts one quantity.
- `--sig N` sets significant figures (default 3), `--min-pipette 0.5uL` the
  smallest volume that's not flagged.

## Recipe files

A `.dilute` file is JSON: `{"format": "dilute", "version": 1, "name",
"notes", "volume": "500 mL", "components": [{"name": "NaCl", "mw": 58.44,
"final": "137 mM", "stock": "5 M"}]}`. A component with `stock` is pipetted
from it; without, a molar or mass concentration is weighed (molar needs
`mw`) and a `%(v/v)` one is added neat. Quantities use a `.` decimal.

## Working with the person

`dilute open [recipe.dilute]` opens the window, which also exports a printable
recipe card (PDF or SVG) with a checkbox per step; it needs their desktop, so
do not wait on it.
