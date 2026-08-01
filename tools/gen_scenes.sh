#!/bin/bash
# gen_scenes.sh — regenerate the playable scene assets from your own disc extract.
#
# WHY THIS EXISTS. assets/ is gitignored (disc-derived, never committed), so a
# fresh clone — or a tree whose assets were cleared — has EMPTY scene directories
# and the game boots to a player standing in the void with no level around them.
# There is no error message for this: the scene loader reports what it loaded, and
# an empty directory simply loads nothing.
#
# Run this after extracting the disc in the sibling decomp repo.
#
#   ./tools/gen_scenes.sh            # default scene (supply room) + office
#
# Requires: the sibling decomp at ../Extermination with extract/ populated
# (its tools/extract_*.py produce that from your own legally-dumped disc).
set -e

PORT="$(cd "$(dirname "$0")/.." && pwd)"
DECOMP="$(cd "$PORT/.." && pwd)/Extermination"
PY="$DECOMP/.venv/bin/python3"

[ -d "$DECOMP/extract" ] || {
  echo "error: $DECOMP/extract not found." >&2
  echo "       Extract your disc first (decomp repo: tools/extract_data.py)." >&2
  exit 1; }
[ -x "$PY" ] || PY=python3

cd "$DECOMP"

# --- default scene: the SUPPLY ROOM -------------------------------------
# AREA02 room 1 entry 3. Its geometry is chunk06.n1 (sub-state 1), NOT the
# chunk06.n0 office main floor: the office mesh spans Z[-195,185] while the
# supply-room spawn record 0x24B760 is at (104, 0, -259), outside it. Exporting
# n0 and spawning there puts the player beyond the level with nothing drawn —
# which looks exactly like "the level stopped loading".
#
# That record's +0x10 = 0x380 also sets the FIXED-camera flag, eye index 3 ->
# D_0024A8D0[3] = (116, 33, -300), the supply-room corner camera.
# See decomp docs/FINDINGS.md "SPAWN-RECORD FIXED CAMERAS".
mkdir -p "$PORT/assets/scene"
"$PY" tools/export_level.py \
    --level extract/chunk06.n1/f03_id43.bin \
    --area 2 --sub 1 \
    --spawn "104,0,-259,3.14159265" \
    --out "$PORT/assets/scene/00_supply.emdl"

# --- the office main floor ---------------------------------------------
# Reachable with EM_SCENE=assets/scene_office0. Sub-state 0, chunk06.n0.
mkdir -p "$PORT/assets/scene_office0"
"$PY" tools/export_level.py \
    --level extract/chunk06.n0/f03_id43.bin \
    --area 2 --sub 0 \
    --spawn "107.4,0,-184,0" \
    --out "$PORT/assets/scene_office0/00_office.emdl"

echo
echo "scenes regenerated:"
echo "  assets/scene            (default — supply room)"
echo "  assets/scene_office0    (EM_SCENE=assets/scene_office0)"
echo
echo "NOTE: collision (.emcl) is not generated here — without it movement falls"
echo "back to the room-bbox clamp. Generate it with the decomp's"
echo "tools/export_collision.py when you need real wall collision."
