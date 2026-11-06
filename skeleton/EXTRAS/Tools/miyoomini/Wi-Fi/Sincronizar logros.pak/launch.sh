#!/bin/sh
# Envía a RetroAchievements los logros conseguidos sin conexión y descarga los
# datos de logros de los juegos nuevos. Funciona con cualquier red con internet.
# Si el Wi-Fi está apagado, primero lo enciende.
. "$(dirname "$0")/../.wifi/wifi.sh"
{
if ! wifi_encendido; then
	encender || exit 0
fi
animar sincronizando
sincronizar_logros
parar_animacion
say.elf "${RES_LOGROS:-Crea .userdata/shared/.ra/cuenta.txt con tu usuario y clave de RetroAchievements}"
} > "$LOGS_PATH/Logros.txt" 2>&1
