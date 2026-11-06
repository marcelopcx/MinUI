#!/bin/sh
# Botón de encendido del Wi-Fi: se llama "Encender" o "Apagar" según el estado.
. "$(dirname "$0")/../.wifi/wifi.sh"
{
if pidof wpa_supplicant >/dev/null 2>&1; then
	apagar
	say.elf "Desconectado"
elif encender; then
	say.elf "Conectado"
fi
} > "$LOGS_PATH/Wi-Fi.txt" 2>&1
