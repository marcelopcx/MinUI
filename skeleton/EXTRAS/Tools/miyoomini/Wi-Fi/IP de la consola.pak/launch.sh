#!/bin/sh
# Muestra la IP de la consola y la dirección del explorador de archivos.
. "$(dirname "$0")/../.wifi/wifi.sh"
IP=$(get_ip)
if [ -n "$IP" ]; then
	say.elf "IP: $IP  -  abre http://$IP"
else
	say.elf "Wi-Fi apagado"
fi
