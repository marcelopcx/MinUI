# MinUI en español para Miyoo Mini Plus

Fork de [MinUI](https://github.com/shauninman/MinUI) (archivado en noviembre de 2025)
adaptado para una Miyoo Mini Plus.

## Cambios respecto a MinUI

**Interfaz en español**
- Menú principal, menú dentro del juego, opciones y descripciones traducidos
  (`workspace/all/minui/minui.c`, `workspace/all/minarch/minarch.c`, `workspace/all/common/api.c`).
- Los valores que se guardan en los `.cfg` siguen en inglés (`*_values` en `minarch.c`),
  así las configuraciones existentes y los `system.cfg` siguen funcionando.
- "Recently Played" pasa a "Jugados recientemente" y la carpeta `Tools` a `Herramientas`.

**Batería en porcentaje**
- La barra superior muestra el porcentaje junto al icono (`GFX_blitHardwareGroup` en `api.c`).
- En Miyoo Mini se usa el valor real de `/tmp/battery` en lugar de redondearlo a 5 niveles
  (`workspace/miyoomini/platform/platform.c`). El aviso de batería baja sigue en 10 %.

**Herramientas (Miyoo Mini)**
- Solo quedan **Wi-Fi**, **Archivos**, **Fecha y hora** y **Batería** (ver el paso `package` del `makefile`).
- **Wi-Fi** (submenú de Herramientas): *Encender o apagar*, *IP de la consola* y
  *Sincronizar con servidor*. Conecta a las redes de `Wi-Fi/.wifi/wifi.txt` (varias, nombre
  y clave en líneas seguidas; solo 2,4 GHz) y abre un explorador de archivos web
  (filebrowser) en `http://<ip-de-la-consola>`. Usa primero el `wpa_supplicant` del
  firmware, que es el que conecta en la Mini Plus; se apaga solo tras 3 min sin conexión.
  Registros en `.userdata/miyoomini/logs/` (`Wi-Fi.txt`, `wpa-N.txt`, `Sincronizar.txt`).
  Binarios incluidos: driver `8188fu.ko`, `wpa_supplicant`/`wpa_cli`/`libnl-tiny.so` y
  `wget` (de Onion OS) y `filebrowser` v2.5.4.
- **Sincronizar con servidor**: llama al servicio `servidor/miyoo-sync` (Docker), que descarga
  a la consola los juegos de la colección "Miyoo" de RomM y sincroniza partidas (`.sav`) y
  estados (`.st0`–`.st9`) en ambos sentidos. Dirección del servicio en `Wi-Fi/.wifi/servidor.txt`.
- **Batería**: muestra porcentaje, voltaje y si carga, y deja un medidor que anota cada minuto
  el consumo en `.userdata/miyoomini/logs/bateria.csv` (para estimar la duración por juego).

**Arcade (Miyoo Mini)**
- Paks `CPS1`, `CPS2`, `CPS3`, `NEOGEO`, `FBN` y `MAME` en `skeleton/EXTRAS/Emus/miyoomini`.
- CPS3 usa FBNeo (el núcleo FB Alpha 2012 CPS-3 se cierra con MinUI) y activa swap.
- Los núcleos no van en el repositorio; se copian de Onion OS con
  `scripts/nucleos-arcade.sh /Volumes/<SD>`.
- FBNeo espera los nombres de romset actuales (ej. `jojou.zip`, `jojobajr1.zip`).

**Pantalla de carga**
- Arcade y PlayStation muestran "Cargando…" mientras el juego se carga, para que la consola
  no parezca congelada.

## Compilar

Igual que MinUI (necesita Docker):

```
make PLATFORMS=miyoomini
```
