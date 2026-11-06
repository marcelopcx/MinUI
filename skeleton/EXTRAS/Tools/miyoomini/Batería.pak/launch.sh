#!/bin/sh
# Batería.pak - muestra el estado de la batería y deja un medidor que anota
# cada minuto el consumo en .userdata/miyoomini/logs/bateria.csv
cd "$(dirname "$0")"
CSV="$LOGS_PATH/bateria.csv"

leer() {
	# axp_test (Mini Plus) devuelve {"battery":85, "voltage":4012, "charging":0}
	AXP=$(/customer/app/axp_test 2>/dev/null)
	PCT=$(echo "$AXP" | sed -n 's/.*"battery":[ ]*\([0-9]*\).*/\1/p')
	MV=$(echo "$AXP" | sed -n 's/.*"voltage":[ ]*\([0-9]*\).*/\1/p')
	CARGA=$(echo "$AXP" | sed -n 's/.*"charging":[ ]*\([0-9]*\).*/\1/p')
	MINUI=$(cat /tmp/battery 2>/dev/null)
}

{
if ! { [ -f /tmp/bateria-medidor.pid ] && kill -0 $(cat /tmp/bateria-medidor.pid) 2>/dev/null; }; then
	[ -f "$CSV" ] || echo "uptime_s;bateria_pct;minui_pct;voltaje_mv;cargando;cpu_mhz;brillo;wifi;emulador" > "$CSV"
	(
		while true; do
			leer
			CPU=$(( $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo 0) / 1000 ))
			BRILLO=$(cat /sys/class/pwm/pwmchip0/pwm0/duty_cycle 2>/dev/null)
			WIFI=$(cat /sys/class/net/wlan0/operstate 2>/dev/null || echo off)
			EMU=$(for p in $(pidof minarch.elf); do tr '\0' ' ' < /proc/$p/cmdline | awk '{print $2}' | sed 's|.*/||; s|_libretro.so||'; done)
			echo "$(cut -d. -f1 /proc/uptime);$PCT;$MINUI;$MV;$CARGA;$CPU;$BRILLO;$WIFI;${EMU:-menu}" >> "$CSV"
			sync
			sleep 60
		done
	) &
	echo $! > /tmp/bateria-medidor.pid
	ESTADO="Medidor activado"
else
	ESTADO="Medidor ya activo"
fi

leer
if [ "$CARGA" = "1" ]; then C=" (cargando)"; else C=""; fi
say.elf "Bateria ${PCT}%  ${MV} mV${C}  -  $ESTADO"
} > "$LOGS_PATH/Bateria.txt" 2>&1
