# Wi-Fi: al arrancar el Wi-Fi está apagado, así que el botón vuelve a "Encender"
M=/mnt/SDCARD/Herramientas/miyoomini/Wi-Fi
[ -d "$M/Apagar.pak" ] && mv "$M/Apagar.pak" "$M/Encender.pak"
