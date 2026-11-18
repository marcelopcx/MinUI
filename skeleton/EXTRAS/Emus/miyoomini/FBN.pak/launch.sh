#!/bin/sh

EMU_EXE=fbneo
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
mkdir -p "$BIOS_PATH/$EMU_TAG"
mkdir -p "$SAVES_PATH/$EMU_TAG"
[ -f /tmp/normal_launch.txt ] || show.elf "$SDCARD_PATH/Emus/miyoomini/cargando.png" # con la animación del tema no se muestra la cortinilla
needs-swap
HOME="$USERDATA_PATH"
cd "$HOME"
nice -20 minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
