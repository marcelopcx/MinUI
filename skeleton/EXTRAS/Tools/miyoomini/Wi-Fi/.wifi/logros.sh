#!/bin/sh
# Sincronización de RetroAchievements (se carga desde wifi.sh).
# minarch detecta los logros sin conexión con los datos de $RA y apunta los
# desbloqueos en pendientes.txt; aquí se envían a la web y se descargan los
# datos de logros de los juegos de la SD.
#
# Cuenta: $RA/cuenta.txt con "usuario=..." y "clave=..." (la clave se borra
# después del primer inicio de sesión; queda solo el token en login.json).

RA=/mnt/SDCARD/.userdata/shared/.ra
RA_URL=https://retroachievements.org/dorequest.php
RA_UA="MinUI-Logros/1.0 rcheevos/12.2"
MINARCH=/mnt/SDCARD/.system/miyoomini/bin/minarch.elf
ZONA_HORARIA=-4 # horas respecto a UTC; la consola guarda la hora local

# Sin hora correcta no se puede conectar por HTTPS (los certificados parecen
# caducados). Si el reloj se perdió (año 1970), se toma la hora de la cabecera
# "Date" de la propia web y se guarda en el reloj de la consola.
ajustar_reloj() {
	[ "$(date +%Y)" -ge 2025 ] && return 0
	D=$("$WIFI_DIR/curl" -skI -m 15 "$RA_URL" | sed -n 's/^[Dd]ate: *//p' | tr -d '\r')
	[ -n "$D" ] || return 1
	E=$(date -u -D "%a, %d %b %Y %H:%M:%S GMT" -d "$D" +%s 2>/dev/null) || return 1
	date -u -s "$(date -u -d "@$((E + ZONA_HORARIA * 3600))" +"%Y-%m-%d %H:%M:%S")" >/dev/null && hwclock -w 2>/dev/null
	paso "reloj ajustado: $(date)"
}

ra() {
	"$WIFI_DIR/curl" -sS -m 30 --cacert "$WIFI_DIR/cacert.pem" -A "$RA_UA" "$@" "$RA_URL" 2>>/tmp/ra-curl.txt
}
json_num() { grep -o "\"$1\":[0-9]*" | head -1 | cut -d: -f2; }
json_txt() { sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p" | head -1; }

ra_login() {
	U=$(sed -n 's/^usuario=//p' "$RA/cuenta.txt" 2>/dev/null | tr -d '\r')
	P=$(sed -n 's/^clave=//p' "$RA/cuenta.txt" 2>/dev/null | tr -d '\r')
	[ -n "$U" ] || U=$(json_txt User 2>/dev/null < "$RA/login.json")
	T=$(json_txt Token 2>/dev/null < "$RA/login.json")
	if [ -n "$P" ]; then
		ra -d r=login2 --data-urlencode "u=$U" --data-urlencode "p=$P" > /tmp/ra-login.json
	elif [ -n "$T" ]; then
		ra -d r=login2 --data-urlencode "u=$U" --data-urlencode "t=$T" > /tmp/ra-login.json
	else
		return 2
	fi
	if grep -q '"Success":true' /tmp/ra-login.json; then
		mv /tmp/ra-login.json "$RA/login.json"
		sed -i '/^clave=/d' "$RA/cuenta.txt" 2>/dev/null
		U=$(json_txt User < "$RA/login.json")
		T=$(json_txt Token < "$RA/login.json")
		return 0
	fi
	[ -s /tmp/ra-login.json ] && return 3 # usuario o clave incorrectos
	paso "sin respuesta de RetroAchievements (fecha de la consola: $(date))"
	"$WIFI_DIR/curl" --version 2>&1 | head -1
	tail -3 /tmp/ra-curl.txt 2>/dev/null
	return 1                             # sin conexión
}

# envía la cola de desbloqueos; deja en ella solo lo que no pudo salir por la red
ra_enviar() {
	ENVIADOS=0
	[ -s "$RA/pendientes.txt" ] || return 0
	: > /tmp/ra-quedan.txt
	AHORA=$(date +%s)
	while read -r t a h m g v; do
		[ -n "$a" ] || continue
		post="a=$a&h=$h"
		[ "$m" != "-" ] && post="$post&m=$m"
		o=$((AHORA - t))
		if command -v md5sum >/dev/null && [ "$o" -gt 0 ] && [ "$o" -lt 2592000 ]; then
			# con "o" la web registra la hora real del desbloqueo
			v=$(printf '%s%s%s%s%s' "$a" "$U" "$h" "$a" "$o" | md5sum | cut -c1-32)
			post="$post&o=$o"
		fi
		[ "$v" != "-" ] && post="$post&v=$v"
		ra -d r=awardachievement --data-urlencode "u=$U" --data-urlencode "t=$T" -d "$post" > /tmp/ra-premio.json
		if [ -s /tmp/ra-premio.json ]; then
			grep -q '"Success":true' /tmp/ra-premio.json && ENVIADOS=$((ENVIADOS + 1))
			paso "logro $a: $(head -c 200 /tmp/ra-premio.json)"
		else
			echo "$t $a $h $m $g $v" >> /tmp/ra-quedan.txt
		fi
	done < "$RA/pendientes.txt"
	mv /tmp/ra-quedan.txt "$RA/pendientes.txt"
}

# hash de RetroAchievements de cada juego de la SD (solo de los nuevos o cambiados)
ra_indexar() {
	: > /tmp/ra-indice.txt
	for dir in /mnt/SDCARD/Roms/*/; do
		TAG=$(basename "$dir" | sed -n 's/.*(\([^)]*\))$/\1/p')
		[ -n "$TAG" ] || continue
		find "$dir" -maxdepth 2 -type f ! -name '.*' | while read -r f; do
			case "$f" in
				*.txt|*.png|*.jpg|*.xml|*.dat|*.sav|*.srm|*.m3u|*/neogeo.zip) continue ;;
				*.bin) [ -n "$(ls "$(dirname "$f")"/*.cue 2>/dev/null)" ] && continue ;;
			esac
			size=$(ls -ln "$f" | awk '{print $5}')
			h=$(awk -F'\t' -v p="$f" -v s="$size" '$1==p && $2==s {print $3; exit}' "$RA/indice.txt" 2>/dev/null)
			if [ -z "$h" ]; then
				h=$("$MINARCH" --ra-hash "$TAG" "$f" 2>/dev/null)
				[ -n "$h" ] || h=-
			fi
			printf '%s\t%s\t%s\n' "$f" "$size" "$h" >> /tmp/ra-indice.txt
		done
	done
	mv /tmp/ra-indice.txt "$RA/indice.txt"
}

# definiciones de logros de los juegos que aún no las tienen
ra_descargar() {
	NUEVOS=0
	{ cut -f3 "$RA/indice.txt"; cat "$RA/sin-datos.txt" 2>/dev/null; } | grep -E '^[0-9a-f]{32}$' | sort -u > /tmp/ra-hashes.txt
	while read -r h; do
		[ -s "$RA/juegos/$h.json" ] && continue
		ra -d r=achievementsets --data-urlencode "u=$U" --data-urlencode "t=$T" -d "m=$h" > /tmp/ra-juego.json
		grep -q '"Success"' /tmp/ra-juego.json || continue
		mv /tmp/ra-juego.json "$RA/juegos/$h.json"
		[ "$(json_num GameId < "$RA/juegos/$h.json")" -gt 0 ] 2>/dev/null && NUEVOS=$((NUEVOS + 1))
	done < /tmp/ra-hashes.txt
	rm -f "$RA/sin-datos.txt"
}

# logros ya desbloqueados en la web, para no repetirlos en la consola
ra_desbloqueados() {
	for j in "$RA"/juegos/*.json; do
		[ -f "$j" ] || continue
		g=$(json_num GameId < "$j")
		[ "${g:-0}" -gt 0 ] || continue
		[ -s "$RA/web/$g.json" ] && [ ! -f "$RA/locales/$g.txt" ] && continue
		ra -d r=unlocks --data-urlencode "u=$U" --data-urlencode "t=$T" -d "g=$g&h=0" > /tmp/ra-web.json
		grep -q '"Success":true' /tmp/ra-web.json || continue
		mv /tmp/ra-web.json "$RA/web/$g.json"
		# lo desbloqueado en la consola ya está en la web salvo lo que siga en la cola
		awk -v g="$g" '$5==g' "$RA/pendientes.txt" 2>/dev/null | grep -q . || rm -f "$RA/locales/$g.txt"
	done
}

# deja en RES_LOGROS un resumen corto para la pantalla
sincronizar_logros() {
	RES_LOGROS=""
	[ -f "$RA/cuenta.txt" ] || [ -f "$RA/login.json" ] || return 0
	mkdir -p "$RA/juegos" "$RA/web" "$RA/locales"
	ajustar_reloj
	ra_login
	case $? in
		1) RES_LOGROS="Logros: sin conexión con RetroAchievements"; return 1 ;;
		2) RES_LOGROS="Logros: falta usuario en .ra/cuenta.txt"; return 1 ;;
		3) RES_LOGROS="Logros: usuario o clave incorrectos"; return 1 ;;
	esac
	ra_enviar
	ra_indexar
	ra_descargar
	ra_desbloqueados
	sync
	QUEDAN=$(grep -c . "$RA/pendientes.txt" 2>/dev/null)
	RES_LOGROS="Logros: $ENVIADOS enviados"
	[ "${QUEDAN:-0}" -gt 0 ] && RES_LOGROS="$RES_LOGROS, $QUEDAN pendientes"
	[ "$NUEVOS" -gt 0 ] && RES_LOGROS="$RES_LOGROS, $NUEVOS juegos nuevos"
	paso "$RES_LOGROS"
	return 0
}
