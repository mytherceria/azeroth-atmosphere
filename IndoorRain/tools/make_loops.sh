#!/bin/bash
# Turns the nine client weather loops (from pull_weather.py) into the muffled indoor loops the addon shipped up
# to 0.13. Since 0.14 the DLL runs these recipes itself (dll/sounds.c) and this script is their record.
# Rain: rumble below 160 Hz cut (thunder), mids kept to 2.5 kHz (the patter), claps compressed, level normalised.
# The compressor takes its options positionally: level_in, mode, threshold, ratio, onset ms, release ms, makeup dB.
# Snow and sand: a plain 750 Hz low-pass, which is how they were heard and approved on 26 Sep 2026.
# What it writes is a filtered copy of Blizzard's sounds, so it refuses an output folder inside a git work tree,
# where a commit could pick the files up (README, License). OUT is judged where it lands: realpath -m takes each part
# that exists as the kernel will (its symlinks, and a '..' after them) and each that does not as mkdir -p will make
# it; then a .git (a folder, or the file a worktree has) is looked for at and above that on disk, not asked of git,
# whose failure (a moved main checkout, a repo it calls dubious, no git at all) must not read as "outside" (the third
# 0.14 review). No file is written through a link at its name (below).
set -eu
IN="${1:?folder with the raw .wav files from pull_weather.py}"
OUT="${2:?output folder: a scratch folder outside any git work tree (the files are copies of Blizzard sounds)}"
d="$(realpath -m -- "$OUT")" || { echo "refusing $OUT: cannot tell where it lands" >&2; exit 2; }
while :; do
  if [ -e "$d/.git" ] || [ -L "$d/.git" ]; then
    echo "refusing $OUT: it lands inside a git work tree, and these files are copies of Blizzard sounds; name a scratch folder outside any repo" >&2
    exit 2
  fi
  if [ "$d" = / ]; then break; fi
  d="$(dirname -- "$d")"
done
mkdir -p "$OUT"
# ffmpeg writes each file into a folder of the run's own inside OUT, which mktemp makes new under a name no other
# program knows, and the file is then renamed to its name in OUT. A rename replaces whatever is at that name, a
# symlink or a second name of another file (a hard link), where ffmpeg -y would write through either, even one put
# there after the check above (the fourth 0.14 review: a symlink was refused only if it was there before ffmpeg ran,
# and a hard link not at all). With -T, mv never moves the file into a folder of that name: it fails instead.
S="$(mktemp -d -- "$OUT/.make_loops.XXXXXX")"; trap 'rm -rf -- "$S"' EXIT
put() { mv -fT -- "$S/$1" "$OUT/$1"; }
for lvl in light medium heavy; do
  ffmpeg -hide_banner -loglevel error -y -i "$IN"/rain_${lvl}_*.wav \
    -af "highpass=f=160:p=2,lowpass=f=2500,acompressor=1:downward:-22dB:8:3:400:2,loudnorm=I=-16:TP=-1.5:LRA=6" \
    -c:a libvorbis -q:a 4 "$S/indoor_rain_${lvl}.ogg"
  put "indoor_rain_${lvl}.ogg"
  for kind in snow sand; do
    ffmpeg -hide_banner -loglevel error -y -i "$IN"/${kind}_${lvl}_*.wav -af "lowpass=f=750:p=2" -c:a libvorbis -q:a 4 "$S/indoor_${kind}_${lvl}.ogg"
    put "indoor_${kind}_${lvl}.ogg"
  done
done
ls -la "$OUT"/*.ogg
