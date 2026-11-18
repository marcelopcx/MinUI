#!/bin/sh
cd "$(dirname "$0")"
HOME="$SDCARD_PATH"
export SDCARD_PATH
./temas.elf
