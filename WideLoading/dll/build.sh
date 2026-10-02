#!/bin/bash
# Builds WideLoading.dll for the 32-bit 1.12.1 client with mingw-w64 and no C runtime, the same way as
# IndoorRain/dll/build.sh: ./build.sh [out.dll]
set -eu
cd "$(dirname "$0")"
out="${1:-WideLoading.dll}"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Wframe-larger-than=4096 -std=c99 -shared -nostdlib -nostartfiles -fno-ident -fno-asynchronous-unwind-tables \
  -fno-stack-protector -fno-builtin -ffreestanding -fno-tree-loop-distribute-patterns -mno-stack-arg-probe \
  -s -Wl,--exclude-all-symbols -Wl,--kill-at -Wl,--subsystem,windows -Wl,-e,_DllMainCRTStartup@12 -o "$out" \
  wideloading.c -lkernel32 -luser32
i686-w64-mingw32-objdump -p "$out" | grep -E "DLL Name" | tr '\n' ' '; echo
ls -la "$out"
