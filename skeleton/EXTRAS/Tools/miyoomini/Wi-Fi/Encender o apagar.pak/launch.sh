#!/bin/sh
# Enciende el Wi-Fi (y el explorador de archivos web) o lo apaga si ya estaba encendido.
. "$(dirname "$0")/../.wifi/wifi.sh"
{
if pidof wpa_supplicant >/dev/null 2>&1; then
	apagar
	say.elf "Wi-Fi apagado"
elif encender; then
	say.elf "Wi-Fi conectado - http://$IP"
fi
} > "$LOGS_PATH/Wi-Fi.txt" 2>&1
