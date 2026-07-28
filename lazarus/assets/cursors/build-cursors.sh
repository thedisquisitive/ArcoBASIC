#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
out="$base/cursors"
work="$base/.generated"

command -v convert >/dev/null 2>&1 || { echo "ImageMagick convert is required" >&2; exit 1; }
command -v xcursorgen >/dev/null 2>&1 || { echo "xcursorgen is required" >&2; exit 1; }

rm -rf "$out" "$work"
mkdir -p "$out" "$work"

pointer() {
    convert -size 32x32 xc:none \
        -stroke '#f5f7fa' -strokewidth 2 -fill '#101820' \
        -draw 'polygon 4,2 4,28 11,22 16,31 21,29 16,20 28,20' \
        -stroke '#2b6cb0' -strokewidth 2 -fill '#163c63' \
        -draw 'polygon 7,7 7,22 11,18 16,27 18,26 13,17 23,17' \
        -stroke '#d32f2f' -fill '#d32f2f' -strokewidth 1 \
        -draw 'circle 10,23 12,23 circle 14,25 16,25' "$1"
}

simple() {
    kind="$1"
    file="$2"
    case "$kind" in
        text) convert -size 32x32 xc:none -stroke '#f5f7fa' -strokewidth 2 -fill none -draw 'line 16,3 16,29 line 8,4 24,4 line 8,28 24,28' "$file" ;;
        cross) convert -size 32x32 xc:none -stroke '#f5f7fa' -strokewidth 2 -fill none -draw 'line 16,3 16,29 line 3,16 29,16' -stroke '#2b6cb0' -strokewidth 2 -draw 'circle 14,14 18,18' "$file" ;;
        move) convert -size 32x32 xc:none -stroke '#f5f7fa' -fill '#163c63' -strokewidth 2 -draw 'polygon 16,2 22,9 19,9 19,13 23,13 23,10 30,16 23,22 23,19 19,19 19,23 22,23 16,30 10,23 13,23 13,19 9,19 9,22 2,16 9,10 9,13 13,13 13,9 10,9' "$file" ;;
        hresize) convert -size 32x32 xc:none -stroke '#f5f7fa' -fill '#163c63' -strokewidth 2 -draw 'polygon 2,16 10,8 10,13 22,13 22,8 30,16 22,24 22,19 10,19 10,24' "$file" ;;
        vresize) convert -size 32x32 xc:none -stroke '#f5f7fa' -fill '#163c63' -strokewidth 2 -draw 'polygon 16,2 24,10 19,10 19,22 24,22 16,30 8,22 13,22 13,10 8,10' "$file" ;;
        unavailable) convert -size 32x32 xc:none -stroke '#f5f7fa' -fill '#d32f2f' -strokewidth 2 -draw 'circle 3,3 29,29' -stroke '#f5f7fa' -strokewidth 3 -draw 'line 8,8 24,24' "$file" ;;
        wait) convert -size 32x32 xc:none -stroke '#f5f7fa' -fill '#163c63' -strokewidth 2 -draw 'line 8,3 24,3 line 8,29 24,29 polygon 10,4 22,4 19,14 19,18 22,28 10,28 13,18 13,14' "$file" ;;
    esac
}

busy() {
    pointer "$1"
    convert "$1" -stroke '#2b6cb0' -fill none -strokewidth 2 -draw 'arc 2,2 30,30 25,285' "$1"
}

make_cursor() {
    name="$1"
    source="$2"
    xhot="$3"
    yhot="$4"
    config="$work/$name.conf"
    : > "$config"
    for size in 24 32 48; do
        png="$work/$name-$size.png"
        convert "$source" -resize "${size}x${size}" "$png"
        hotx=$((xhot * size / 32))
        hoty=$((yhot * size / 32))
        printf '%s %s %s %s\n' "$size" "$hotx" "$hoty" "$png" >> "$config"
    done
    xcursorgen "$config" "$out/$name"
}

pointer "$work/pointer.png"
busy "$work/progress.png"
simple text "$work/text.png"
simple cross "$work/cross.png"
simple move "$work/move.png"
simple hresize "$work/hresize.png"
simple vresize "$work/vresize.png"
simple unavailable "$work/unavailable.png"
simple wait "$work/wait.png"

make_cursor left_ptr "$work/pointer.png" 4 3
make_cursor help "$work/pointer.png" 4 3
make_cursor progress "$work/progress.png" 4 3
make_cursor watch "$work/wait.png" 16 16
make_cursor text "$work/text.png" 16 16
make_cursor crosshair "$work/cross.png" 16 16
make_cursor move "$work/move.png" 16 16
make_cursor sb_h_double_arrow "$work/hresize.png" 16 16
make_cursor sb_v_double_arrow "$work/vresize.png" 16 16
make_cursor not-allowed "$work/unavailable.png" 16 16

for alias in default arrow top_left_arrow pointer; do ln -sf left_ptr "$out/$alias"; done
for alias in question_arrow dnd-ask; do ln -sf help "$out/$alias"; done
for alias in left_ptr_watch half-busy; do ln -sf progress "$out/$alias"; done
for alias in wait clock; do ln -sf watch "$out/$alias"; done
for alias in xterm vertical-text; do ln -sf text "$out/$alias"; done
for alias in fleur all-scroll; do ln -sf move "$out/$alias"; done
for alias in col-resize ew-resize e-resize w-resize; do ln -sf sb_h_double_arrow "$out/$alias"; done
for alias in row-resize ns-resize n-resize s-resize; do ln -sf sb_v_double_arrow "$out/$alias"; done
for alias in forbidden no-drop; do ln -sf not-allowed "$out/$alias"; done

rm -rf "$work"
echo "Built Arcology-Lazarus cursor theme: $out"
