#!/bin/sh
# Sincroniza partidas y juegos con RomM (servicio miyoo-sync del servidor).
# Si el Wi-Fi está apagado, primero lo enciende.
. "$(dirname "$0")/../.wifi/wifi.sh"
{
if ! wifi_encendido; then
	encender || exit 0
fi
sincronizar
say.elf "$RES"
} > "$LOGS_PATH/Sincronizar.txt" 2>&1
