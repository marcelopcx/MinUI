#!/bin/sh
# Funciones comunes del menú Wi-Fi (Miyoo Mini Plus). Lo cargan los tres paks:
#   Encender/Apagar (el nombre cambia según el estado), IP de la consola y
#   Sincronizar con RomM.
# Redes en wifi.txt (nombre en una línea, clave en la siguiente; solo 2,4 GHz).
# Servidor de sincronización (miyoo-sync + RomM) en servidor.txt.

WIFI_DIR=$(cd "$(dirname "$0")/../.wifi" && pwd)
cd "$WIFI_DIR"
export LD_LIBRARY_PATH="$WIFI_DIR:$LD_LIBRARY_PATH"
CONF=/appconfigs/wpa_supplicant.conf
WGET=$(command -v wget || echo "$WIFI_DIR/wget")

MENU_DIR=$(dirname "$WIFI_DIR")

paso() { echo "== $(date +%H:%M:%S) $*"; }

# el botón de encendido se llama "Encender" con el Wi-Fi apagado y "Apagar" con él encendido
nombre_boton() {
	for n in Encender Apagar; do
		[ "$n" != "$1" ] && [ -d "$MENU_DIR/$n.pak" ] && mv "$MENU_DIR/$n.pak" "$MENU_DIR/$1.pak"
	done
	sync
}

# texto animado ("Conectando", "Sincronizando") con los tres puntos moviéndose
animar() {
	touch /tmp/wifi-animando
	(
		while [ -f /tmp/wifi-animando ]; do
			for n in 1 2 3; do
				[ -f /tmp/wifi-animando ] || break
				show.elf "$WIFI_DIR/$1-$n.png"
				sleep 0.4
			done
		done
	) &
	ANIM_PID=$!
}
parar_animacion() {
	rm -f /tmp/wifi-animando
	[ -n "$ANIM_PID" ] && wait $ANIM_PID 2>/dev/null
	ANIM_PID=""
}

get_ip() {
	ip -4 addr show dev wlan0 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1
}

wifi_encendido() {
	pidof wpa_supplicant >/dev/null 2>&1 && [ -n "$(get_ip)" ]
}

apagar() {
	killall -9 filebrowser wpa_supplicant udhcpc >/dev/null 2>&1
	ifconfig wlan0 down >/dev/null 2>&1
	/customer/app/axp_test wifioff >/dev/null 2>&1
	nombre_boton Encender
}

# intenta conectar: $1 = programa wpa_supplicant, $2 = driver (nl80211 o wext)
INTENTO=0
intentar() {
	INTENTO=$((INTENTO + 1))
	killall -9 wpa_supplicant udhcpc >/dev/null 2>&1
	rm -f /var/run/wpa_supplicant/wlan0
	paso "probando $1 con driver $2"
	# en segundo plano y con registro detallado
	$1 -D $2 -iwlan0 -c $CONF -d > "$LOGS_PATH/wpa-$INTENTO.txt" 2>&1 &
	sleep 1
	pidof wpa_supplicant >/dev/null || return 1
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
	paso "estado"; ./wpa_cli -i wlan0 status; ./wpa_cli -i wlan0 list_networks
	paso "motivo (de wpa-$INTENTO.txt)"
	grep -E "CTRL-EVENT|reason|WRONG_KEY|TEMP-DISABLED|Authentication|Association|4-Way|PMK|deauth|blacklist|skip" "$LOGS_PATH/wpa-$INTENTO.txt" | tail -25
	return 1
}

# conecta a la primera red de wifi.txt disponible y arranca el explorador de
# archivos web. Deja la IP en $IP. Si falla, muestra el motivo y devuelve 1.
encender() {
	REDES=$(tr -d '\r' < wifi.txt 2>/dev/null | grep -v '^#' | grep -v '^[[:space:]]*$')
	SSID=$(echo "$REDES" | sed -n 1p)
	if [ -z "$SSID" ] || [ "$SSID" = "NOMBRE_DE_TU_RED" ]; then
		say.elf "Edita Herramientas/miyoomini/Wi-Fi/.wifi/wifi.txt"
		return 1
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

	animar conectando
	paso "cargando driver"
	lsmod | grep -q 8188fu || insmod ./8188fu.ko
	/customer/app/axp_test wifion
	sleep 2
	ifconfig lo up
	ifconfig wlan0 up

	SYS_WPA=$(command -v wpa_supplicant)
	IP=""; SIN_REDES=""
	rm -f "$LOGS_PATH"/wpa-*.txt
	# primero el del firmware (es el que conecta en la Mini+); los otros, de respaldo
	{ [ -n "$SYS_WPA" ] && intentar "$SYS_WPA" nl80211; } ||
		intentar ./wpa_supplicant nl80211 ||
		intentar ./wpa_supplicant wext
	paso "mensajes del driver"; dmesg | grep -iE "8188|wlan0|rtl" | tail -15

	if [ -n "$SIN_REDES" ]; then
		apagar; parar_animacion; say.elf "No hay redes conocidas cerca"; return 1
	fi
	if [ -z "$IP" ]; then
		ESTADO=$(./wpa_cli -i wlan0 status 2>/dev/null | sed -n 's/^wpa_state=//p')
		apagar; parar_animacion
		if grep -q "ASSOC-REJECT" "$LOGS_PATH"/wpa-*.txt 2>/dev/null; then
			say.elf "El router rechazó la conexión"
		else
			case "$ESTADO" in
				COMPLETED) say.elf "Conectado pero sin IP" ;;
				4WAY_HANDSHAKE|ASSOCIATED) say.elf "Contraseña incorrecta" ;;
				*) say.elf "No se pudo conectar" ;;
			esac
		fi
		return 1
	fi

	paso "conectado: $IP"
	./filebrowser -p 80 -a 0.0.0.0 -r /mnt/SDCARD -d "$WIFI_DIR/filebrowser.db" --noauth >/dev/null 2>&1 &

	# vigilante: si se pierde la conexión 3 minutos seguidos (por ejemplo al salir
	# de casa), apaga el Wi-Fi para no gastar batería buscando la red
	(
		FALLOS=0
		while pidof wpa_supplicant >/dev/null 2>&1; do
			sleep 60
			E=$(./wpa_cli -i wlan0 status 2>/dev/null | sed -n 's/^wpa_state=//p')
			if [ "$E" = "COMPLETED" ]; then FALLOS=0; else FALLOS=$((FALLOS + 1)); fi
			if [ $FALLOS -ge 3 ]; then
				echo "== $(date +%H:%M:%S) sin conexion 3 min: Wi-Fi apagado" >> "$LOGS_PATH/Wi-Fi.txt"
				apagar
				break
			fi
		done
	) >/dev/null 2>&1 &
	sleep 1
	nombre_boton Apagar
	parar_animacion
	return 0
}

# pide al servidor que sincronice partidas y juegos; deja el resumen en $RES
sincronizar() {
	SRV=$(grep -v '^#' servidor.txt 2>/dev/null | tr -d '\r' | awk 'NF' | head -1)
	RES=""
	if [ -z "$SRV" ]; then
		RES="Sin servidor configurado (.wifi/servidor.txt)"; return 1
	fi
	animar sincronizando
	# justo después de conectar el chip puede tardar en ver al servidor: ping y reintentos
	OK=""
	for t in 1 2 3 4 5 6; do
		ping -c 1 -W 2 "${SRV%%:*}" >/dev/null 2>&1
		$WGET -q -T 5 -O /tmp/sync.txt "http://$SRV/sync?ip=$(get_ip)" && { OK=1; break; }
		paso "intento $t: servidor $SRV no responde"
		sleep 2
	done
	if [ -z "$OK" ]; then
		parar_animacion
		RES="No se pudo contactar con RomM"; return 1
	fi
	for i in $(seq 1 180); do
		sleep 5
		$WGET -q -T 5 -O /tmp/sync.txt "http://$SRV/estado" || continue
		head -1 /tmp/sync.txt | grep -q LISTO && break
	done
	RES=$(sed -n 2p /tmp/sync.txt)
	paso "sincronización: $RES"
	parar_animacion
	return 0
}
