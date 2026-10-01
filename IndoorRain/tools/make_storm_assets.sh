#!/bin/bash
# Builds the storm sounds (0.13): louder outdoor rain loops with no thunder baked in, thunder one-shots
# (plus muffled copies for indoors), wind gusts, and new indoor rain loops made from thunder-free sources.
#
# Why: RavenCraft's patch-S rain loops carry thunder on a fixed cycle (medium: a roll 7-13 s into every
# 31 s loop; heavy: rolls at 12-21 s and 42-56 s of 75 s), so the sky thundered like clockwork, indoors too.
# The storm now comes from the DLL at random, and every rain loop here is thunder-free:
#   light, medium: the original game's loops (patch.MPQ), flat, no thunder;
#   heavy: RavenCraft's 48 kHz loop with both rolls cut out (0-11.5, 22.5-41.5 and 57-75 s kept).
# Inputs (raw client files, from the MPQs):
#   vanilla_light.wav   patch.MPQ  Sound\Ambience\Weather\RainLightLoop.wav
#   vanilla_medium.wav  patch.MPQ  Sound\Ambience\Weather\RainMediumLoop.wav
#   heavy.wav           patch-S    Sound\Ambience\Weather\RainHeavyLoop.wav
#   bolt0..3.wav        sound.MPQ  Sound\Doodad\BlastedLandsLightningbolt01Stand-Bolt{,1,2,3}.wav
#   crack.wav           patch-S    Sound\Spells\CallLightning.wav
#   gust1..3.ogg        patch-S    Sound\Spells\SPELL_SH_Revamp_Wind_PreCast0{1,2,3}.ogg
# Outdoor loudness (integrated, before the client's 0.69 x AmbienceVolume): light -21, medium -17, heavy -14 LUFS.
# Since 0.14 the DLL runs these recipes itself (dll/sounds.c) and this script is their record. What it writes is a
# filtered copy of Blizzard's sounds, so it refuses an output folder inside a git work tree, where a commit could
# pick the files up (README, License). OUT is judged where it lands: realpath -m takes each part that exists as the
# kernel will (its symlinks, and a '..' after them) and each that does not as mkdir -p will make it; then a .git (a
# folder, or the file a worktree has) is looked for at and above that on disk, not asked of git, whose failure (a
# moved main checkout, a repo it calls dubious, no git at all) must not read as "outside" (the third 0.14 review). No
# file is written through a link at its name (below).
set -eu
IN="${1:?folder with the raw client files listed above}"
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
mkdir -p "$OUT"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# The files in OUT: ffmpeg writes each into a folder of the run's own inside OUT, which mktemp makes new under a name
# no other program knows, and the file is then renamed to its name in OUT. A rename replaces whatever is at that name,
# a symlink or a second name of another file (a hard link), where ffmpeg -y would write through either, even one put
# there after the check above (the fourth 0.14 review: a symlink was refused only if it was there before ffmpeg ran,
# and a hard link not at all). With -T, mv never moves the file into a folder of that name: it fails instead.
S="$(mktemp -d -- "$OUT/.make_storm_assets.XXXXXX")"; trap 'rm -rf -- "$T" "$S"' EXIT
put() { mv -fT -- "$S/$1" "$OUT/$1"; }
FF="ffmpeg -hide_banner -loglevel error -y"
VORBIS="-c:a libvorbis -q:a 5 -ar 44100"

# Seamless loop: the last X seconds are crossfaded over the first X, so the file's end runs into its start.
loopify() {  # in out seconds
  local in=$1 out=$2 x=$3 len
  len=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$in")
  $FF -i "$in" -filter_complex "[0]asplit=3[a][b][c];[a]atrim=0:$x,asetpts=N/SR/TB[h];\
[b]atrim=$(awk "BEGIN{print $len - $x}"):$len,asetpts=N/SR/TB[t];[c]atrim=$x:$(awk "BEGIN{print $len - $x}"),asetpts=N/SR/TB[body];\
[t][h]acrossfade=d=$x:c1=qsin:c2=qsin[x];[x][body]concat=n=2:v=0:a=1" -ar 44100 "$out"
}
# Gain to an integrated loudness, true peak held under -1 dB by a limiter.
to_lufs() {  # in out lufs
  local in=$1 out=$2 target=$3 now
  now=$(ffmpeg -hide_banner -nostats -i "$in" -af ebur128 -f null - 2>&1 | grep -E '^\s+I:' | tail -1 | awk '{print $2}')
  $FF -i "$in" -af "volume=$(awk "BEGIN{print $target - ($now)}")dB,alimiter=limit=0.89:level=false" -ar 44100 "$out"
}

# heavy: cut both rolls; B + C crossfaded, then A (C's end is the original loop point into A)
$FF -i "$IN/heavy.wav" -filter_complex "[0]asplit=3[x][y][z];[x]atrim=22.5:41.5,asetpts=N/SR/TB[b];\
[y]atrim=57:75,asetpts=N/SR/TB[c];[z]atrim=0:11.5,asetpts=N/SR/TB[a];[b][c]acrossfade=d=1.5:c1=qsin:c2=qsin[bc];\
[bc][a]concat=n=2:v=0:a=1" "$T/heavy_clean.wav"
loopify "$T/heavy_clean.wav" "$T/heavy_loop.wav" 1.5
$FF -i "$IN/vanilla_medium.wav" -ar 44100 "$T/medium_loop.wav"
$FF -i "$IN/vanilla_light.wav" -ar 44100 "$T/light_loop.wav"

# outdoor: the client plays these in place of its own loops
to_lufs "$T/light_loop.wav"  "$T/ol.wav" -21; $FF -i "$T/ol.wav" $VORBIS "$S/outdoor_rain_light.ogg"; put outdoor_rain_light.ogg
to_lufs "$T/medium_loop.wav" "$T/om.wav" -17; $FF -i "$T/om.wav" $VORBIS "$S/outdoor_rain_medium.ogg"; put outdoor_rain_medium.ogg
to_lufs "$T/heavy_loop.wav"  "$T/oh.wav" -14; $FF -i "$T/oh.wav" $VORBIS "$S/outdoor_rain_heavy.ogg"; put outdoor_rain_heavy.ogg

# indoor: the same muffling as make_loops.sh, now from the thunder-free loops, at 44.1 kHz
for lvl in medium heavy; do
  $FF -i "$T/${lvl}_loop.wav" -af "highpass=f=160:p=2,lowpass=f=2500,acompressor=1:downward:-22dB:8:3:400:2,loudnorm=I=-16:TP=-1.5:LRA=6" \
    $VORBIS "$S/indoor_rain_${lvl}.ogg"
  put "indoor_rain_${lvl}.ogg"
done

# thunder: close crack, near boom, mid roll, two distant rolls; each with a muffled indoor copy. Outdoors each is
# pushed 6 dB into a limiter: thunder at full channel volume must still stand clear of the louder heavy rain.
i=0
for spec in "crack:close" "bolt0:near" "bolt1:mid" "bolt2:far" "bolt3:far2"; do
  src=${spec%%:*}; name=${spec##*:}
  $FF -i "$IN/$src.wav" -af "highpass=f=25,loudnorm=I=-18:TP=-1:LRA=11,volume=6dB,alimiter=limit=0.89:attack=3:release=120:level=false" -ac 1 $VORBIS "$S/thunder_${name}.ogg"
  put "thunder_${name}.ogg"
  $FF -i "$IN/$src.wav" -af "highpass=f=25,lowpass=f=350:p=2,lowpass=f=350:p=2,loudnorm=I=-19:TP=-2:LRA=11" -ac 1 $VORBIS "$S/thunder_${name}_in.ogg"
  put "thunder_${name}_in.ogg"
done
# gusts: the spell sparkle above 3 kHz trimmed, a gentle fade at both ends
for n in 1 2 3; do
  $FF -i "$IN/gust$n.ogg" -af "highpass=f=70,lowpass=f=3000,afade=t=in:d=0.4,areverse,afade=t=in:d=0.8,areverse,loudnorm=I=-20:TP=-2:LRA=9" \
    -ac 1 $VORBIS "$S/gust_$n.ogg"
  put "gust_$n.ogg"
done
ls -la "$OUT"/outdoor_*.ogg "$OUT"/indoor_rain_*.ogg "$OUT"/thunder_*.ogg "$OUT"/gust_*.ogg
