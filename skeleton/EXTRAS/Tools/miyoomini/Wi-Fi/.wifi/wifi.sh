#!/bin/sh
# Funciones comunes del menú Wi-Fi (Miyoo Mini Plus). Lo cargan los paks del menú:
#   Encender/Apagar (el nombre cambia según el estado), IP de la consola y
#   Sincronizar con RomM y Sincronizar logros.
# Redes en wifi.txt (nombre en una línea, clave en la siguiente; solo 2,4 GHz).
# Servidor de sincronización (miyoo-sync + RomM) en servidor.txt.
# Logros de RetroAchievements: logros.sh (cuenta en .userdata/shared/.ra/cuenta.txt).

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
		say.elf "Edita Herramientas/miyoomini/1) Wi-Fi/.wifi/wifi.txt"
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
	ajustar_reloj
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

# primera línea que no es https (la casa) o la primera https (desde fuera)
direccion_servidor() {
	awk -v cual="$1" '
		/^#/ || NF == 0 { next }
		/^https:\/\// { if (cual == "https") { print; exit } ; next }
		{ if (cual == "lan") { print; exit } }
	' servidor.txt 2>/dev/null | tr -d '\r'
}

hash_archivo() {
	if command -v md5sum >/dev/null 2>&1; then
		md5sum "$1" | awk '{print $1}'
	elif command -v md5 >/dev/null 2>&1; then
		md5 -q "$1"
	fi
}

# deja la ruta en ASCII para ?ruta= (espacios y paréntesis incluidos)
codificar() {
	printf '%s' "$1" | awk 'BEGIN {
		for (i = 1; i < 256; i++) hex[sprintf("%c", i)] = sprintf("%02X", i)
	}
	{
		n = length($0)
		for (i = 1; i <= n; i++) {
			ch = substr($0, i, 1)
			if (ch ~ /^[A-Za-z0-9._~-]$/) printf "%s", ch
			else printf "%%%s", hex[ch]
		}
	}'
}

# la consola guarda la hora local en el reloj UTC, y el certificado público
# queda "todavía no válido". Se pone la hora real solo durante esta conexión.
HORA_ANTERIOR=""
poner_hora_real() {
	D=$("$CURL" -skI -m 15 "$1" | sed -n 's/^[Dd]ate: *//p' | tr -d '\r' | head -1)
	[ -n "$D" ] || return 1
	E=$(date -u -D "%a, %d %b %Y %H:%M:%S GMT" -d "$D" +%s 2>/dev/null) || return 1
	HORA_ANTERIOR=$(date +"%Y-%m-%d %H:%M:%S")
	date -u -s "$(date -u -d "@$E" +"%Y-%m-%d %H:%M:%S")" >/dev/null
	paso "reloj temporal para la conexión segura"
}
restaurar_hora() {
	[ -n "$HORA_ANTERIOR" ] || return 0
	E=$(date -u +%s 2>/dev/null) || E=0
	ZONA=${ZONA_HORARIA:--4}
	date -u -s "$(date -u -d "@$((E + ZONA * 3600))" +"%Y-%m-%d %H:%M:%S")" >/dev/null 2>&1
	hwclock -w 2>/dev/null
	HORA_ANTERIOR=""
}

# desde fuera: la consola sube y baja; el servidor no entra a su IP local
sincronizar_remoto() {
	URL=${1%/}
	CLAVE=$(tr -d '\r\n ' < "$WIFI_DIR/sync.clave")
	CURL="$WIFI_DIR/curl"
	[ -x "$CURL" ] || CURL=$(command -v curl) || { RES="Falta curl para la sincronización remota"; return 1; }
	poner_hora_real "$URL/remoto/ping" || { RES="No se pudo ajustar el reloj"; return 1; }
	# la carpeta se llama "1) Wi-Fi": el certificado va entre comillas o curl parte la ruta
	cert() {
		if [ -f "$WIFI_DIR/cacert.pem" ]; then
			"$CURL" --cacert "$WIFI_DIR/cacert.pem" "$@"
		else
			"$CURL" "$@"
		fi
	}
	SD=${SDCARD_PATH:-/mnt/SDCARD}
	{
		for d in "$SD/Roms"/*; do
			[ -d "$d" ] || continue
			for f in "$d"/*; do
				[ -f "$f" ] || continue
				n=$(basename "$f")
				case "$n" in .*|map.txt) continue ;; esac
				printf 'ROM\t%s\n' "${f#"$SD/"}"
			done
		done
		for f in "$SD/Saves"/*/* "$SD/.userdata/shared"/*/*; do
			[ -f "$f" ] || continue
			rel=${f#"$SD/"}
			case "$rel" in *.sav|*.st[0-9]) ;; *) continue ;; esac
			sum=$(hash_archivo "$f")
			[ -n "$sum" ] && printf 'FILE\t%s\t%s\n' "$rel" "$sum"
		done
	} > /tmp/manifiesto.txt
	cert -fsS -m 180 -H "X-Sync-Token: $CLAVE" --data-binary @/tmp/manifiesto.txt \
		"$URL/remoto/plan" -o /tmp/plan.txt || { RES="No se pudo contactar con RomM"; restaurar_hora; return 1; }
	juegos=0; subidas=0; bajadas=0; conflictos=0; errores=0
	while IFS=$(printf '\t') read -r op ruta; do
		[ -n "$op" ] || continue
		q=$(codificar "$ruta")
		case "$op" in
		JUEGO|BAJAR)
			mkdir -p "$SD/$(dirname "$ruta")"
			if cert -fS -m 3600 -D /tmp/sync-hdr -H "X-Sync-Token: $CLAVE" \
				-o "$SD/$ruta.parcial" "$URL/remoto/bajar?ruta=$q"; then
				mv "$SD/$ruta.parcial" "$SD/$ruta"
				sum=$(hash_archivo "$SD/$ruta")
				printf '%s\t%s' "$ruta" "$sum" | cert -fsS -m 60 -H "X-Sync-Token: $CLAVE" \
					--data-binary @- "$URL/remoto/confirmar" >/dev/null || true
				if [ "$op" = JUEGO ]; then juegos=$((juegos + 1)); else bajadas=$((bajadas + 1)); fi
			else
				rm -f "$SD/$ruta.parcial"
				errores=$((errores + 1))
			fi
			;;
		SUBIR)
			resp=$(cert -fsS -m 300 -H "X-Sync-Token: $CLAVE" -T "$SD/$ruta" \
				"$URL/remoto/subir?ruta=$q") || { errores=$((errores + 1)); continue; }
			case "$resp" in
				*conflicto*) conflictos=$((conflictos + 1)) ;;
				*subida*) subidas=$((subidas + 1)) ;;
				*BAJAR*) errores=$((errores + 1)) ;;
			esac
			;;
		esac
	done < /tmp/plan.txt
	RES=""
	[ "$juegos" -gt 0 ] && RES="$RES$juegos nuevos, "
	[ "$subidas" -gt 0 ] && RES="$RES$subidas subidas, "
	[ "$bajadas" -gt 0 ] && RES="$RES$bajadas bajadas, "
	[ "$conflictos" -gt 0 ] && RES="$RES$conflictos conflictos, "
	[ "$errores" -gt 0 ] && RES="$RES$errores errores, "
	RES=${RES%, }
	[ -n "$RES" ] || RES="Al día"
	restaurar_hora
	return 0
}

# pide al servidor que sincronice partidas y juegos; deja el resumen en $RES
sincronizar() {
	LAN=$(direccion_servidor lan)
	HTTPS=$(direccion_servidor https)
	RES=""
	if [ -z "$LAN" ] && [ -z "$HTTPS" ]; then
		RES="Sin servidor configurado (.wifi/servidor.txt)"; return 1
	fi
	animar sincronizando
	if [ -n "$LAN" ]; then
		OK=""
		for t in 1 2 3; do
			ping -c 1 -W 2 "${LAN%%:*}" >/dev/null 2>&1
			$WGET -q -T 5 -O /tmp/sync.txt "http://$LAN/sync?ip=$(get_ip)" && { OK=1; break; }
			paso "intento $t: servidor $LAN no responde"
			sleep 1
		done
		if [ -n "$OK" ]; then
			for i in $(seq 1 180); do
				sleep 5
				$WGET -q -T 5 -O /tmp/sync.txt "http://$LAN/estado" || continue
				head -1 /tmp/sync.txt | grep -q LISTO && break
			done
			RES=$(sed -n 2p /tmp/sync.txt)
			paso "sincronización: $RES"
			parar_animacion
			return 0
		fi
	fi
	if [ -n "$HTTPS" ] && [ -s "$WIFI_DIR/sync.clave" ]; then
		paso "casa no responde, sincronizando por la dirección pública"
		sincronizar_remoto "$HTTPS"
		paso "sincronización: $RES"
		parar_animacion
		return $?
	fi
	parar_animacion
	RES="No se pudo contactar con RomM"
	return 1
}

. "$WIFI_DIR/logros.sh"
