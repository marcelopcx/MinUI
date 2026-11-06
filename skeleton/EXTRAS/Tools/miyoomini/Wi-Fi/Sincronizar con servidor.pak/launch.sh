#!/bin/sh
# Sincroniza partidas y juegos con el servidor (miyoo-sync + RomM).
# Si el Wi-Fi está apagado, primero lo enciende.
. "$(dirname "$0")/../.wifi/wifi.sh"
{
if ! wifi_encendido; then
	encender || exit 0
fi
if sincronizar; then
	say.elf "Sincronizado: $RES"
else
	say.elf "$RES"
fi
} > "$LOGS_PATH/Sincronizar.txt" 2>&1
