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

**Orden de los menús**
- Un nombre que empiece por `N) ` (p. ej. `1) Wi-Fi`) se ordena por ese número y se muestra
  sin el prefijo (`workspace/all/minui/minui.c`, `workspace/all/common/utils.c`). Así las
  herramientas van por uso: Wi-Fi, Batería, Archivos, Fecha y hora.

**Herramientas (Miyoo Mini)**
- Solo quedan **Wi-Fi**, **Archivos**, **Fecha y hora** y **Batería** (ver el paso `package` del `makefile`).
- **Wi-Fi** (submenú de Herramientas): *Encender* / *Apagar* (el nombre cambia según el
  estado), *IP de la consola* y *Sincronizar con RomM*. Mientras conecta o sincroniza muestra
  "Conectando…" / "Sincronizando…" con los puntos animados, y al final solo "Conectado" o el
  error. Conecta a las redes de `Wi-Fi/.wifi/wifi.txt` (varias, nombre y clave en líneas
  seguidas; solo 2,4 GHz) y abre un explorador de archivos web (filebrowser) en
  `http://<ip-de-la-consola>`. Usa primero el `wpa_supplicant` del firmware, que es el que
  conecta en la Mini Plus; se apaga solo tras 3 min sin conexión. Para que el botón vuelva a
  "Encender" al arrancar, copia `Wi-Fi/.wifi/auto.sh` a `.userdata/miyoomini/auto.sh`.
  Registros en `.userdata/miyoomini/logs/` (`Wi-Fi.txt`, `wpa-N.txt`, `Sincronizar.txt`,
  `Logros.txt`).
  Binarios incluidos: driver `8188fu.ko`, `wpa_supplicant`/`wpa_cli`/`libnl-tiny.so` y
  `wget` (de Onion OS) y `filebrowser` v2.5.4.
- **Sincronizar con RomM**: llama al servicio `servidor/miyoo-sync` (Docker), que descarga
  a la consola los juegos de la colección "Miyoo" de RomM y sincroniza partidas (`.sav`) y
  estados (`.st0`–`.st9`) en ambos sentidos. Dirección del servicio en `Wi-Fi/.wifi/servidor.txt`.
- **Batería**: muestra porcentaje, voltaje y si carga, y deja un medidor que anota cada minuto
  el consumo en `.userdata/miyoomini/logs/bateria.csv` (para estimar la duración por juego).

**Logros de RetroAchievements (sin conexión)**
- minarch lleva [rcheevos](https://github.com/RetroAchievements/rcheevos) (`logros.c`): los
  logros se detectan mientras juegas sin necesidad de internet y aparece el aviso
  "Logro desbloqueado: …" con un sonido de logro (sintetizado, se mezcla con el juego). El
  menú del juego muestra "Logros: X de Y".
- Los desbloqueos se guardan en `.userdata/shared/.ra/pendientes.txt` y se envían a
  retroachievements.org con **Wi-Fi → Sincronizar logros** (aparte de "Sincronizar con
  RomM"), con la hora real del desbloqueo. Funciona con cualquier red con internet.
- Esa misma sincronización identifica los juegos de la SD (`minarch.elf --ra-hash TAG rom`,
  incluidos los `.chd` con libchdr) y descarga sus logros, para que funcionen sin conexión.
  Los juegos en `.zip` (salvo arcade) se identifican la primera vez que se juegan y quedan
  listos en la siguiente sincronización.
- Cuenta: crea `.userdata/shared/.ra/cuenta.txt` con `usuario=...` y `clave=...`. Tras el
  primer inicio de sesión la clave se borra y solo queda el token.
- Modo normal (softcore): los estados guardados siguen funcionando; el progreso de los
  logros se guarda junto a cada estado (`.ra`).
- `curl` y sus librerías vienen de Onion OS; `cacert.pem` son los certificados raíz.

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
