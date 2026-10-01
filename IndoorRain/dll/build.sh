#!/bin/bash
# Builds IndoorRain.dll for the 32-bit 1.12.1 client with mingw-w64, with no C runtime at all.
# Extra -D flags (e.g. -DADDR_CMAPWEATHER_PTR=0x...) pass through: ./build.sh out.dll [CFLAGS...]
# Since 0.14 the DLL is five sources: indoorrain.c, the MPQ reader (mpq.c, inflate.c) and the sound recipes
# (dsp.c, sounds.c) it builds its sounds with. -Wframe-larger-than: there are no stack probes
# (-mno-stack-arg-probe), so a function whose frame passed 4 KB could step over the stack's guard page.
set -eu
cd "$(dirname "$0")"
out="${1:-IndoorRain.dll}"; shift || true
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Wframe-larger-than=4096 -std=c99 -shared -nostdlib -nostartfiles -fno-ident -fno-asynchronous-unwind-tables \
  -fno-stack-protector -fno-builtin -ffreestanding -fno-tree-loop-distribute-patterns -mno-stack-arg-probe "$@" \
  -s -Wl,--exclude-all-symbols -Wl,--kill-at -Wl,--subsystem,windows -Wl,-e,_DllMainCRTStartup@12 -o "$out" \
  indoorrain.c mpq.c inflate.c dsp.c sounds.c -lkernel32 -luser32
i686-w64-mingw32-objdump -p "$out" | grep -E "DLL Name" | tr '\n' ' '; echo
ls -la "$out"
