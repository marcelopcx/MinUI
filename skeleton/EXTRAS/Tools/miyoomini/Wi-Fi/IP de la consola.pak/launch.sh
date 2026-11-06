#!/bin/sh
# Muestra la IP que el router le dio a la consola.
. "$(dirname "$0")/../.wifi/wifi.sh"
IP=$(get_ip)
say.elf "${IP:-Sin conexión}"
