#!/bin/sh
#
# build-asset.sh -- one command from generated PNG frames to shippable assets.
#
#   tools/build-asset.sh <frames-dir> <asset-name> [options]
#
# Takes a directory of generated PNG frames (PixelLab, Retro Diffusion, hand
# work -- anything), TRUES them first, then derives every tier the game ships:
#
#   color  32x32 indexed PICT, System 7 palette   -> Colour QuickDraw Macs
#   se     16x16 1-bit BitMap PICT, Atkinson      -> Mac SE (original QuickDraw)
#   sdl    64x64 BMP sprite atlas                 -> modern SDL client
#
# and regenerates the Rez data blocks. Nothing untrued reaches a build: the
# truing pass scores how far each frame was off-grid and off-palette, and a
# red verdict fails the run so a sloppy generation gets regenerated instead of
# shipped. Sizes, ids, palettes and low-memory policy come from
# resources/gfx/assets.json.
#
# Truing runs ONCE for the whole directory against a single voted grid, so an
# animation cannot wobble between frames; each tier is then derived from the
# trued frames.
#
# Options:
#   --tiers a,b,c     tiers to build (default: every tier in the manifest)
#   --score-max N     fail if any frame's correction score exceeds N
#                     (default 35 = pixeltrue's red threshold)
#   --min-confidence F
#                     fail if the detected grid's confidence is below F
#                     (default 0.15). Catches input that is not pixel art at
#                     all, which a correction score alone cannot.
#   --allow-amber     do not fail on amber verdicts (default: amber warns)
#   --knockout SPEC   override the manifest: auto, none, or #RRGGBB
#   --keep-work       keep the intermediate directory for inspection
#   --no-embed        emit PICTs but skip regenerating the .r files
#   -h, --help        this text
#
# Environment:
#   PIXELTRUE   path to the pixeltrue binary (default: search ../pixeltrue's
#               release build, then PATH)
#
# Requires: pixeltrue (github.com/matthewdeaves/pixeltrue), ImageMagick,
# python3. See docs/asset-pipeline.md.

set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
GFX_DIR="$REPO_ROOT/resources/gfx"
MANIFEST="$GFX_DIR/assets.json"
PNG2PICT="$REPO_ROOT/tools/png2pict.py"

die() { echo "build-asset: $*" >&2; exit 1; }
note() { echo "  $*"; }

usage() { sed -n '3,45p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

# ---- arguments ----------------------------------------------------------

[ $# -ge 1 ] || usage 1
case "$1" in -h|--help) usage 0 ;; esac
[ $# -ge 2 ] || usage 1

FRAMES_DIR=$1
ASSET=$2
shift 2

TIERS=""
SCORE_MAX=35
MIN_CONFIDENCE=0.15
ALLOW_AMBER=0
KNOCKOUT=""
KEEP_WORK=0
DO_EMBED=1

while [ $# -gt 0 ]; do
    case "$1" in
        --tiers)       TIERS=${2:?--tiers needs a value}; shift 2 ;;
        --score-max)   SCORE_MAX=${2:?--score-max needs a value}; shift 2 ;;
        --min-confidence) MIN_CONFIDENCE=${2:?--min-confidence needs a value}; shift 2 ;;
        --allow-amber) ALLOW_AMBER=1; shift ;;
        --knockout)    KNOCKOUT=${2:?--knockout needs a value}; shift 2 ;;
        --keep-work)   KEEP_WORK=1; shift ;;
        --no-embed)    DO_EMBED=0; shift ;;
        -h|--help)     usage 0 ;;
        *)             die "unknown option '$1' (try --help)" ;;
    esac
done

[ -d "$FRAMES_DIR" ] || die "no such frames directory: $FRAMES_DIR"
[ -f "$MANIFEST" ] || die "no manifest: $MANIFEST"

# ---- tools --------------------------------------------------------------

if [ -n "${PIXELTRUE:-}" ]; then
    :
elif [ -x "$REPO_ROOT/../pixeltrue/.build/release/pixeltrue" ]; then
    PIXELTRUE="$REPO_ROOT/../pixeltrue/.build/release/pixeltrue"
elif command -v pixeltrue >/dev/null 2>&1; then
    PIXELTRUE=pixeltrue
else
    die "pixeltrue not found. Build it (swift build -c release in ../pixeltrue)
             or set PIXELTRUE=/path/to/pixeltrue"
fi
command -v magick >/dev/null 2>&1 || die "ImageMagick (magick) not on PATH"
command -v python3 >/dev/null 2>&1 || die "python3 not on PATH"

# ---- manifest queries ---------------------------------------------------

# ask <jq-ish python expression over the asset dict> -- prints one line.
ask() {
    python3 - "$MANIFEST" "$ASSET" "$1" <<'PY'
import json, sys
manifest, asset_name, query = sys.argv[1], sys.argv[2], sys.argv[3]
with open(manifest) as f:
    data = json.load(f)
asset = next((a for a in data.get('assets', []) if a['name'] == asset_name), None)
if asset is None:
    names = ', '.join(a['name'] for a in data.get('assets', []))
    sys.exit(f'asset "{asset_name}" is not in assets.json (have: {names})')

tiers = asset.get('tiers', {})
if query == 'frames':
    print(asset.get('frames', 1))
elif query == 'knockout':
    print(asset.get('knockout', 'auto'))
elif query == 'tiers':
    print(' '.join(tiers))
elif query.startswith('tier:'):
    _, tier, field = query.split(':', 2)
    spec = tiers.get(tier)
    if spec is None:
        sys.exit(f'asset "{asset_name}" has no "{tier}" tier')
    value = spec.get(field, '')
    print(' '.join(str(v) for v in value) if isinstance(value, list) else value)
else:
    sys.exit(f'internal: unknown query {query}')
PY
}

FRAMES=$(ask frames)
ALL_TIERS=$(ask tiers)
[ -n "$KNOCKOUT" ] || KNOCKOUT=$(ask knockout)
[ -n "$TIERS" ] || TIERS=$ALL_TIERS
TIERS=$(echo "$TIERS" | tr ',' ' ')

INPUT_COUNT=$(find "$FRAMES_DIR" -maxdepth 1 -type f \
    \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.heic' \) | wc -l | tr -d ' ')
[ "$INPUT_COUNT" -gt 0 ] || die "no images in $FRAMES_DIR"
[ "$INPUT_COUNT" -eq "$FRAMES" ] || \
    die "$FRAMES_DIR holds $INPUT_COUNT image(s) but assets.json declares
             $FRAMES frame(s) for '$ASSET'"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/build-asset-$ASSET.XXXXXX")
cleanup() {
    if [ "$KEEP_WORK" -eq 1 ]; then
        echo "work kept: $WORK"
    else
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT

echo "build-asset: $ASSET -- $INPUT_COUNT frame(s) from $FRAMES_DIR"
echo "  pixeltrue: $PIXELTRUE"
echo "  tiers:     $TIERS"

# ---- stage 1: true ------------------------------------------------------
#
# One voted grid across every frame (--true), no palette yet: truing maps the
# generator's own colours so the correction score measures GRID error, and each
# tier enforces its own palette afterwards.

echo
echo "true: detecting one grid across all frames"
if [ "$KNOCKOUT" = "none" ]; then
    "$PIXELTRUE" batch "$FRAMES_DIR" \
        --true --grid auto \
        --out-dir "$WORK/trued" \
        --report "$WORK/true.json" >/dev/null
else
    "$PIXELTRUE" batch "$FRAMES_DIR" \
        --true --grid auto \
        --knockout "$KNOCKOUT" \
        --out-dir "$WORK/trued" \
        --report "$WORK/true.json" >/dev/null
fi

python3 - "$WORK/true.json" "$SCORE_MAX" "$ALLOW_AMBER" "$MIN_CONFIDENCE" <<'PY'
import json, sys
report_path, score_max = sys.argv[1], float(sys.argv[2])
allow_amber, min_confidence = sys.argv[3] == '1', float(sys.argv[4])
with open(report_path) as f:
    report = json.load(f)

grid = report.get('grid')
if grid:
    cx, cy = grid.get('confidenceX'), grid.get('confidenceY')
    shown = f" confidence {cx:.2f},{cy:.2f}" if cx is not None else ''
    print(f"  grid:  pitch {grid['pitchX']:g}x{grid['pitchY']:g} "
          f"offset {grid['offsetX']:g},{grid['offsetY']:g}{shown}")
    # The correction score cannot tell "trued cleanly" from "there was no grid
    # to find" -- a gridless picture still yields a pitch, and its cells still
    # cohere at it. Confidence is the field that separates them.
    if cx is not None and min(cx, cy) < min_confidence:
        sys.exit(f"build-asset: REJECTED -- grid confidence {cx:.3f},{cy:.3f} is below "
                 f"{min_confidence:g}.\n"
                 f"             No block structure was found, so there is nothing to snap "
                 f"to and\n"
                 f"             the correction score below means little. These frames are "
                 f"probably not\n"
                 f"             pixel art at all (a photo, or a smooth render). Lower the "
                 f"bar with\n"
                 f"             --min-confidence if you know better.")

worst, amber = [], []
for frame in report.get('frames', []):
    score = frame.get('correctionScore')
    verdict = frame.get('verdict', '?')
    name = frame['input'].rsplit('/', 1)[-1]
    shown = f'{score:6.1f}' if score is not None else '     -'
    print(f"  frame: {name:<28} score {shown}  {verdict}")
    if score is None:
        continue
    if score > score_max or verdict == 'red':
        worst.append((name, score, verdict))
    elif verdict == 'amber':
        amber.append((name, score))

sys.stdout.flush()   # keep the per-frame table above the warnings below
if amber:
    listed = ', '.join(f'{n} ({s:.1f})' for n, s in amber)
    print(f"  warning: amber frame(s) worth a hand-check in Aseprite: {listed}",
          file=sys.stderr)
    if not allow_amber:
        print("  (pass --allow-amber to accept them without this note)", file=sys.stderr)

if worst:
    listed = ', '.join(f'{n} score {s:.1f} [{v}]' for n, s, v in worst)
    sys.exit(f"build-asset: REJECTED -- {listed}.\n"
             f"             These frames are too far off-grid or off-palette to true "
             f"cleanly.\n"
             f"             Regenerate them, or raise the bar with --score-max.")
PY

# ---- stage 2: derive each tier -----------------------------------------

emit_pict() {
    # emit_pict <mode> <src-png> <dst-pict>
    _mode=$1; _src=$2; _dst=$3
    python3 "$PNG2PICT" "$_src" "$_dst" --mode "$_mode"
    note "$(basename "$_dst") ($(wc -c < "$_dst" | tr -d ' ') bytes, $_mode)"
}

frame_basename() {
    # frame_basename <tier> <index> -- matches scripts/embed-gfx.py's convention
    if [ "$FRAMES" -eq 1 ]; then
        echo "${ASSET}_$1"
    else
        echo "${ASSET}_$1_f$2"
    fi
}

EMITTED_PICTS=0
for TIER in $TIERS; do
    case " $ALL_TIERS " in
        *" $TIER "*) ;;
        *) die "'$ASSET' has no '$TIER' tier in assets.json (has: $ALL_TIERS)" ;;
    esac

    SIZE=$(ask "tier:$TIER:size")
    PALETTE=$(ask "tier:$TIER:palette")
    DITHER=$(ask "tier:$TIER:dither")
    [ -n "$DITHER" ] || DITHER=none
    ATLAS=$(ask "tier:$TIER:atlas")

    echo
    echo "$TIER: $SIZE, palette $PALETTE, dither $DITHER"

    if [ -n "$ATLAS" ]; then
        "$PIXELTRUE" batch "$WORK/trued" \
            --size "$SIZE" --palette "$PALETTE" --dither "$DITHER" \
            --resample box --fit contain \
            --atlas "$GFX_DIR/$ATLAS" --atlas-columns "$FRAMES" \
            --out-dir "$WORK/$TIER" >/dev/null
        note "$ATLAS (sprite atlas)"
    else
        "$PIXELTRUE" batch "$WORK/trued" \
            --size "$SIZE" --palette "$PALETTE" --dither "$DITHER" \
            --resample box --fit contain \
            --out-dir "$WORK/$TIER" >/dev/null
    fi

    # The SDL tier ships the atlas, not PICTs.
    if [ "$TIER" = "sdl" ]; then
        continue
    fi

    if [ "$TIER" = "se" ]; then
        MODE=1bit
    else
        MODE=indexed
    fi

    # Fed by find(1) through a pipe rather than $(...) so frame names
    # containing spaces survive.
    INDEX=0
    find "$WORK/$TIER" -maxdepth 1 -name '*.png' | sort > "$WORK/$TIER.list"
    while IFS= read -r PNG; do
        emit_pict "$MODE" "$PNG" \
            "$GFX_DIR/$(frame_basename "$TIER" "$INDEX").pict"
        EMITTED_PICTS=$((EMITTED_PICTS + 1))
        INDEX=$((INDEX + 1))
    done < "$WORK/$TIER.list"

    if [ "$INDEX" -ne "$FRAMES" ]; then
        die "$TIER tier produced $INDEX frame(s), expected $FRAMES"
    fi
done

# ---- stage 3: embed -----------------------------------------------------

echo
if [ "$DO_EMBED" -eq 1 ] && [ "$EMITTED_PICTS" -gt 0 ]; then
    python3 "$REPO_ROOT/scripts/embed-gfx.py"
elif [ "$EMITTED_PICTS" -gt 0 ]; then
    echo "skipped embedding (--no-embed); run scripts/embed-gfx.py when ready"
fi

echo
echo "done. Next: rebuild (./build-all.sh) and check the asset on hardware --"
echo "      the SE tier is the one that needs real eyes (docs/asset-pipeline.md)."
