#!/usr/bin/env bash
# 2TUFF.wav - make-free build script.
# Compiles src/*.c with psp-gcc and packages an EBOOT.PBP using the pspdev
# toolchain. Run inside WSL (or any Linux with pspdev installed).
set -euo pipefail

export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PSPDEV/bin:$PATH"

# psp-gcc's cc1 needs libmpc.so.3, which a stock Ubuntu/WSL may not ship.
# Stage it locally (no sudo) and expose it via LD_LIBRARY_PATH so the build is
# reproducible without touching the system. Falls back to an apt hint.
EXTRA_LIB="$HOME/pspdev-extra/usr/lib/x86_64-linux-gnu"
if [ ! -e /usr/lib/x86_64-linux-gnu/libmpc.so.3 ] && [ ! -e "$EXTRA_LIB/libmpc.so.3" ]; then
    echo ">> staging libmpc3 (psp-gcc host dependency, no sudo)"
    mkdir -p "$HOME/pspdev-extra"
    if wget -q -O "$HOME/pspdev-extra/libmpc3.deb" \
        "http://archive.ubuntu.com/ubuntu/pool/main/m/mpclib3/libmpc3_1.3.1-1build1_amd64.deb"; then
        dpkg-deb -x "$HOME/pspdev-extra/libmpc3.deb" "$HOME/pspdev-extra" || true
    else
        echo "!! could not download libmpc3; if the build fails, run: sudo apt-get install -y libmpc3"
    fi
fi
[ -d "$EXTRA_LIB" ] && export LD_LIBRARY_PATH="$EXTRA_LIB:${LD_LIBRARY_PATH:-}"

PSPSDK="$(psp-config --pspsdk-path)"

TARGET="2TUFFwav"
TITLE="2TUFF.wav"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p build "dist/PSP/GAME/$TARGET"

CFLAGS="-G0 -O2 -Wall -Wno-format-truncation -fno-strict-aliasing -D_PSP_FW_VERSION=600 \
  -I$ROOT/src -I$PSPDEV/psp/include -I$PSPSDK/include"
LDFLAGS="-L$PSPDEV/psp/lib -L$PSPSDK/lib -Wl,-zmax-page-size=128"
# Order matters for psp-fixup-imports: our app/portlibs first, the standard
# SDK stub libraries last (matching $(PSPSDK)/lib/build.mak).
LIBS="-lpspgu -lpspmp3 -lpspaudio -lpsputility -lpsppower -ljpeg -lpng16 -lz -lm \
  -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspnet -lpspnet_apctl"

echo ">> compiling"
OBJS=()
for f in src/*.c; do
    o="build/$(basename "${f%.c}").o"
    echo "   CC  $f"
    psp-gcc $CFLAGS -c "$f" -o "$o"
    OBJS+=("$o")
done

echo ">> linking"
psp-gcc $LDFLAGS "${OBJS[@]}" $LIBS -o "build/$TARGET.elf"
psp-fixup-imports "build/$TARGET.elf"

echo ">> packaging EBOOT.PBP"
mksfoex -d MEMSIZE=1 "$TITLE" "build/PARAM.SFO"
psp-strip "build/$TARGET.elf" -o "build/${TARGET}_strip.elf"

ICON="NULL"
[ -f assets/ICON0.PNG ] && ICON="assets/ICON0.PNG"
PIC1="NULL"
[ -f assets/PIC1.PNG ] && PIC1="assets/PIC1.PNG"

# pack-pbp slot order: PARAM.SFO ICON0 ICON1 PIC0 PIC1 SND0 DATA.PSP DATA.PSAR
pack-pbp "build/EBOOT.PBP" \
    "build/PARAM.SFO" "$ICON" NULL NULL "$PIC1" NULL \
    "build/${TARGET}_strip.elf" NULL

cp "build/EBOOT.PBP" "dist/PSP/GAME/$TARGET/EBOOT.PBP"
rm -f "build/${TARGET}_strip.elf"

echo ">> done"
echo "   build/EBOOT.PBP            $(stat -c%s build/EBOOT.PBP) bytes"
echo "   dist/PSP/GAME/$TARGET/EBOOT.PBP  (copy this PSP folder to your Memory Stick)"
