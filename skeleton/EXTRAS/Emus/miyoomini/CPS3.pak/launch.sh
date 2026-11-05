#!/bin/sh

EMU_EXE=fbneo
CORES_PATH="$(dirname "$0")/../FBN.pak"

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
mkdir -p "$BIOS_PATH/$EMU_TAG"
mkdir -p "$SAVES_PATH/$EMU_TAG"
show.elf "$SDCARD_PATH/Emus/miyoomini/cargando.png"
needs-swap
HOME="$USERDATA_PATH"
cd "$HOME"
nice -20 minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
