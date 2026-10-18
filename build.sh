#!/usr/bin/env bash
# Сборка прокси ddraw.dll (i686, mingw-w64) -> build/ddraw.dll
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
python3 -I tools/check_addr.py   # адреса кода в исходниках — только через BB_CODE()/BB_A() и BB_CODES
i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
    -shared -static-libgcc -Wl,--enable-stdcall-fixup \
    -o build/ddraw.dll src/*.c src/ddraw.def -lgdi32
i686-w64-mingw32-strip build/ddraw.dll
ls -la build/ddraw.dll
