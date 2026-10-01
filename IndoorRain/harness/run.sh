#!/bin/bash
# Builds the Wine harness and runs it headless, in a fresh folder of its own, against a client's archives.
#   harness/run.sh <client folder>               e.g. harness/run.sh ~/Games/RavenCraft
#   harness/run.sh --build-only <client folder>  builds the run folder, checks that Wine would reach no display,
#                                                and stops before Wine; the flag counts before or after the folder
# Every argument is read: anything else that starts with a dash, or a second folder, stops the script before it
# builds anything, since a flag it skipped over could be the one that keeps Wine from starting.
# The client folder is only read: its fmod.dll is copied into the run folder, and the test DLL opens its
# Data\*.mpq read-only through data_dir. Two runs, each in its own folder (the DLL starts once per process):
# the full walk, then --no-data. A walk still running after 15 minutes (limit, below) is stopped and counts as
# one failed check. Both outputs land in <run>/last-run.txt, with the run, client and home folders written as <run>,
# <client> and ~, so it can be copied over harness/last-run.txt as it is. The Wine prefix is deleted when the
# script ends, on Ctrl-C and on a TERM or HUP too, and another of those while it is being deleted does not stop that
# (a KILL leaves it, since nothing can catch one); the rest of the run folder stays. The exit code is the number of
# failed checks of both runs together.
set -eu
limit=900   # seconds a walk may take: its checks take about four minutes, a new prefix and a cold disk add some
usage="usage: run.sh [--build-only] <client folder holding fmod.dll and Data>   (--build-only may also come after the folder)"
build_only=0; client=""
for a in "$@"; do
  case "$a" in
    --build-only) build_only=1 ;;
    -h|--help) echo "$usage"; exit 0 ;;
    -*) echo "unknown option $a"; echo "$usage"; exit 2 ;;
    *) if [ -n "$client" ]; then echo "one client folder only, not also $a"; echo "$usage"; exit 2; fi; client="$a" ;;
  esac
done
if [ -z "$client" ]; then echo "$usage"; exit 2; fi
client="$(cd "$client" && pwd)"
if [ ! -f "$client/fmod.dll" ] || [ ! -d "$client/Data" ]; then echo "no fmod.dll or no Data folder in $client"; exit 2; fi
if [ "$build_only" = 0 ] && ! command -v wine >/dev/null; then echo "no wine on PATH"; exit 2; fi
here="$(cd "$(dirname "$0")" && pwd)"
run="$(mktemp -d -t indoorrain-harness.XXXXXX)"
prefix="$run/prefix"
mkdir -p "$run/lib" "$run/full" "$run/nodata" "$run/xdg"
(cd "$run/lib" && TMPDIR="$run/lib" i686-w64-mingw32-dlltool --no-leading-underscore -d "$here/fmod.def" -l libfmod.a)   # dlltool leaves two empty temp files behind: here, not in /tmp
# -O0: at -O2 gcc calls FSOUND_Init through a copy of the import it loaded before the DLL redirected the slot
i686-w64-mingw32-gcc -O0 -Wall -o "$run/lib/harness.exe" "$here/harness.c" -L"$run/lib" -lfmod
"$here/../dll/build.sh" "$run/lib/IndoorRain.dll" -DADDR_CMAPWEATHER_PTR=0x00C7B100u -DADDR_WEATHER_SOUND_ID=0x00C7B200u \
  -DADDR_OBJMGR_PTR=0x00C7B300u -DHARNESS_CVARS=0x00C7B700u
gcc -O2 -Wall -o "$run/lib/display_probe" "$here/display_probe.c" -ldl   # native: the one check that runs here, not under Wine
for d in full nodata; do cp "$run/lib/harness.exe" "$run/lib/IndoorRain.dll" "$client/fmod.dll" "$run/$d/"; done
data="Z:$(printf '%s' "$client/Data" | tr '/' '\\')"   # the same folder as Wine's Z: drive names it
echo "run folder: $run"
echo "data_dir:   $data"

# Headless, with nothing of the desktop session in reach. Unsetting DISPLAY and WAYLAND_DISPLAY is not enough:
# with WAYLAND_DISPLAY unset, libwayland tries wayland-0 in XDG_RUNTIME_DIR, and on a Wayland desktop that is
# the user's own compositor, where Wine's first-run window or a crash dialog would open (and a dialog would stall
# the run). So WAYLAND_SOCKET goes too, and XDG_RUNTIME_DIR is an empty folder of the run's own, which also keeps
# the PulseAudio and PipeWire sockets out of reach (the harness plays nothing: FMOD's nosound output). Unsetting
# XDG_RUNTIME_DIR instead would send libpulse to make its runtime folder under ~/.config/pulse.
headless() { env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$run/xdg" "$@"; }
# display_probe asks, in that environment, what Wine's X11 and Wayland drivers would ask: Wine starts only if
# neither finds a display.
if ! headless "$run/lib/display_probe"; then echo "not headless: in the environment Wine would get, a display is in reach; Wine not started"; exit 3; fi
if [ "$build_only" = 1 ]; then exit 0; fi

# The prefix is about 380 MB, and /tmp is memory on many systems: it goes when the script ends, however it ends,
# once its wineserver (which outlives the last program by a few seconds) is stopped. The rest of the run stays.
# Ctrl-C ends the whole run through it: without the INT trap, a Ctrl-C that Wine answers by exiting would let the
# script go on to the next walk. A kill needs no trap of its own: bash, killed by a TERM or HUP it does not trap, runs
# the EXIT trap at once, and the wineserver -k there ends the walk; a trapped TERM would wait for the walk in the
# foreground to end first, up to its limit (the third 0.14 review). Once cleanup has begun, nothing but a KILL stops
# it: it ignores INT, TERM and HUP first, and so do the wineserver -k and rm it starts, since a signal ignored stays
# ignored in a child. Without that, a second Ctrl-C during it had the INT trap exit again, and a TERM or a HUP (the
# terminal closed) killed bash, before rm had removed the prefix (the fourth 0.14 review).
cleanup() {
  trap '' INT TERM HUP
  if [ -d "$prefix" ]; then WINEPREFIX="$prefix" wineserver -k >/dev/null 2>&1 || true; rm -rf -- "$prefix"; fi
}
trap cleanup EXIT; trap 'exit 130' INT
# In Wine itself: no display driver at all (the harness opens no window; one that something tried to open fails
# instead of reaching a desktop), no winemenubuilder, which a new prefix runs to write menu entries and file
# associations into the user's home, and no Mono or Gecko prompt.
export WINEPREFIX="$prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe,winex11.drv,winewayland.drv=d"
# One walk in its own folder, its output in <run>/<folder>.txt, stopped at the limit (--foreground, so Ctrl-C
# still reaches it). Returns the walk's failed checks; a walk stopped at the limit counts as one (the harness
# has about a hundred checks, so 124 and 137, timeout's codes, are never a count).
walk() {   # walk <folder> <harness arguments...>
  local d="$1" rc=0; shift
  (cd "$run/$d" && headless timeout --foreground -k 30 "$limit" wine harness.exe "$@") > "$run/$d.txt" 2>&1 || rc=$?
  if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then echo "stopped after $limit s: counted as one failed check" >> "$run/$d.txt"; rc=1; fi
  return "$rc"
}
# The outputs name the run, client and home folders, in Linux's form and in Wine's (Z:\home\...), and those carry
# the login name. Each becomes <run>, <client> or ~, in that order, so the copy kept in the repo names none.
scrub() {   # scrub <file>, in place
  local text i w
  local -a from=("$run" "$client" "${HOME:-}") to=("<run>" "<client>" "~")
  text="$(cat "$1")"
  for i in 0 1 2; do
    if [ -z "${from[$i]}" ] || [ "${from[$i]}" = / ]; then continue; fi
    w="Z:$(printf '%s' "${from[$i]}" | tr '/' '\\')"
    text="${text//"$w"/"${to[$i]}"}"
    text="${text//"${from[$i]}"/"${to[$i]}"}"
  done
  printf '%s\n' "$text" > "$1"
}
full=0; nodata=0
walk full "$data" || full=$?
walk nodata --no-data || nodata=$?
{ echo "== harness.exe $data"; cat "$run/full.txt"; echo; echo "== harness.exe --no-data"; cat "$run/nodata.txt"; } > "$run/last-run.txt"
scrub "$run/last-run.txt"
echo "full walk: $full failed; no-data walk: $nodata failed"
echo "output: $run/last-run.txt (the DLL's own logs: $run/full/Logs/IndoorRain.log and $run/nodata/Logs/IndoorRain.log)"
exit $((full + nodata))
