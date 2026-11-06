#!/bin/sh
# Wi-Fi.pak (Miyoo Mini Plus) - enciende/apaga el Wi-Fi y un explorador de
# archivos web para pasar cosas a la SD desde otro equipo de la red.
# Las redes se configuran en wifi.txt (nombre en una linea, clave en la siguiente).

DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"
export LD_LIBRARY_PATH="$DIR:$LD_LIBRARY_PATH"
LOG="$LOGS_PATH/Wi-Fi.txt"
CONF=/appconfigs/wpa_supplicant.conf

wifi_off() {
	killall -9 filebrowser wpa_supplicant udhcpc >/dev/null 2>&1
	ifconfig wlan0 down >/dev/null 2>&1
	/customer/app/axp_test wifioff >/dev/null 2>&1
}

get_ip() {
	ip -4 addr show dev wlan0 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1
}

paso() { echo "== $(date +%H:%M:%S) $*"; }

{
# si ya esta encendido, este mismo pak lo apaga
if pidof filebrowser >/dev/null 2>&1 || pidof wpa_supplicant >/dev/null 2>&1; then
	wifi_off
	say.elf "Wi-Fi apagado"
	exit 0
fi

# wifi.txt: una red por pareja de lineas (nombre, clave). Las lineas vacias
# y las que empiezan con # se ignoran. La primera red tiene mas prioridad.
REDES=$(tr -d '\r' < wifi.txt 2>/dev/null | grep -v '^#' | grep -v '^[[:space:]]*$')
SSID=$(echo "$REDES" | sed -n 1p)
if [ -z "$SSID" ] || [ "$SSID" = "NOMBRE_DE_TU_RED" ]; then
	say.elf "Edita Herramientas/miyoomini/Wi-Fi.pak/wifi.txt"
	exit 0
fi

{
	echo "ctrl_interface=/var/run/wpa_supplicant"
	echo "update_config=1"
	echo "$REDES" | awk '
		NR % 2 == 1 { ssid = $0; next }
		{ n++; printf "\nnetwork={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n\tscan_ssid=1\n\tpriority=%d\n}\n", ssid, $0, 100 - n }'
} > $CONF
sync
paso "redes configuradas:"; grep 'ssid="' $CONF

show.elf "$DIR/wifi-0.png"
paso "cargando driver"
lsmod | grep -q 8188fu || insmod ./8188fu.ko
lsmod | grep 8188fu
/customer/app/axp_test wifion
sleep 2
ifconfig lo up
ifconfig wlan0 up
ifconfig wlan0 | head -2

# intenta conectar: $1 = programa wpa_supplicant, $2 = driver (nl80211 o wext)
INTENTO=0
intentar() {
	INTENTO=$((INTENTO + 1))
	show.elf "$DIR/wifi-$INTENTO.png"
	killall -9 wpa_supplicant udhcpc >/dev/null 2>&1
	rm -f /var/run/wpa_supplicant/wlan0
	paso "probando $1 con driver $2"
	$1 -B -D $2 -iwlan0 -c $CONF || return 1
	sleep 3
	echo "$REDES" | awk 'NR % 2 == 1' > /tmp/wifi_redes.txt
	# hasta 3 escaneos: el chip no siempre recorre todos los canales en el primero
	CERCA=0
	for e in 1 2 3; do
		./wpa_cli -i wlan0 scan >/dev/null
		sleep 5
		./wpa_cli -i wlan0 scan_results > /tmp/wifi_scan.txt
		while IFS= read -r red; do
			# nombres con tildes salen escapados en el escaneo: no se pueden comparar, se asume que están
			echo "$red" | awk '{ exit !/[\200-\377]/ }' && CERCA=1
			awk -F'\t' -v r="$red" '$5 == r { f = 1 } END { exit !f }' /tmp/wifi_scan.txt && CERCA=1
		done < /tmp/wifi_redes.txt
		[ $CERCA -eq 1 ] && break
	done
	paso "redes visibles tras $e escaneo(s) (MHz: 24xx = 2,4 GHz, 5xxx = 5 GHz)"
	cat /tmp/wifi_scan.txt
	# si tras 3 escaneos no aparece ninguna red conocida, no tiene sentido insistir
	if [ $INTENTO -eq 1 ] && [ $CERCA -eq 0 ] && [ $(wc -l < /tmp/wifi_scan.txt) -gt 1 ]; then
		SIN_REDES=1; return 0
	fi
	udhcpc -i wlan0 -s /etc/init.d/udhcpc.script >/dev/null 2>&1 &
	for i in $(seq 1 20); do
		IP=$(get_ip)
		[ -n "$IP" ] && return 0
		sleep 1
	done
	paso "estado"; ./wpa_cli -i wlan0 status
	return 1
}

SYS_WPA=$(command -v wpa_supplicant)
paso "wpa_supplicant del sistema: ${SYS_WPA:-no hay}"
IP=""
# primero el del firmware (es el que conecta en la Mini+); los otros, de respaldo
{ [ -n "$SYS_WPA" ] && intentar "$SYS_WPA" nl80211; } ||
	intentar ./wpa_supplicant nl80211 ||
	intentar ./wpa_supplicant wext
paso "mensajes del driver"; dmesg | grep -iE "8188|wlan0|rtl" | tail -15

if [ -n "$SIN_REDES" ]; then
	paso "ninguna red conocida a la vista"
	wifi_off
	say.elf "No hay redes conocidas cerca"
	exit 0
fi

if [ -z "$IP" ]; then
	ESTADO=$(./wpa_cli -i wlan0 status 2>/dev/null | sed -n 's/^wpa_state=//p')
	wifi_off
	case "$ESTADO" in
		COMPLETED) say.elf "Conectado pero sin IP. Reintenta" ;;
		4WAY_HANDSHAKE|ASSOCIATED) say.elf "Clave incorrecta?" ;;
		*) say.elf "No veo tus redes. Son de 2,4 GHz?" ;;
	esac
	exit 0
fi

paso "conectado: $IP"
./filebrowser -p 80 -a 0.0.0.0 -r /mnt/SDCARD -d "$DIR/filebrowser.db" --noauth >/dev/null 2>&1 &

# vigilante: si se pierde la conexión 3 minutos seguidos (por ejemplo al salir
# de casa), apaga el Wi-Fi para no gastar batería buscando la red
(
	FALLOS=0
	while pidof wpa_supplicant >/dev/null 2>&1; do
		sleep 60
		E=$(./wpa_cli -i wlan0 status 2>/dev/null | sed -n 's/^wpa_state=//p')
		if [ "$E" = "COMPLETED" ]; then FALLOS=0; else FALLOS=$((FALLOS + 1)); fi
		if [ $FALLOS -ge 3 ]; then
			echo "== $(date +%H:%M:%S) sin conexion 3 min: Wi-Fi apagado" >> "$LOG"
			wifi_off
			break
		fi
	done
) >/dev/null 2>&1 &
sleep 1
say.elf "Wi-Fi OK - abre http://$IP"
} > "$LOG" 2>&1
