#!/bin/bash
# Builds CinderLoad.dll for the 32-bit 1.12.1 client with mingw-w64 and no C runtime, the same way as
# IndoorRain/dll/build.sh: ./build.sh [out.dll]. The fire sound reads the client's archives with IndoorRain's MPQ reader
# (IndoorRain/dll/mpq.c, inflate.c), compiled in here from that folder so the two DLLs share one copy of it.
set -eu
cd "$(dirname "$0")"
out="${1:-CinderLoad.dll}"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Wframe-larger-than=4096 -std=c99 -shared -nostdlib -nostartfiles -fno-ident -fno-asynchronous-unwind-tables \
  -fno-stack-protector -fno-builtin -ffreestanding -fno-tree-loop-distribute-patterns -mno-stack-arg-probe \
  -I../../IndoorRain/dll -s -Wl,--exclude-all-symbols -Wl,--kill-at -Wl,--subsystem,windows -Wl,-e,_DllMainCRTStartup@12 -o "$out" \
  cinderload.c ../../IndoorRain/dll/mpq.c ../../IndoorRain/dll/inflate.c -lkernel32 -luser32
i686-w64-mingw32-objdump -p "$out" | grep -E "DLL Name" | tr '\n' ' '; echo
ls -la "$out"
