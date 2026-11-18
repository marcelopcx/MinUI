# Temas MinUI (Miyoo Mini Plus)

## Uso en la consola

1. Carpeta `Temas/` en la raíz de la SD (junto a `Roms/`, `Bios/`, …).
2. **Herramientas → Temas**: elige un tema y pulsa **A** para aplicar (reinicia el menú).
3. **Clásico** restaura el aspecto original (sin fondo ni paleta personalizada).

El tema activo se guarda en `.userdata/miyoomini/theme.txt`.

## Crear un tema

Crea una carpeta `Temas/Mi-Tema/` con:

| Archivo | Obligatorio | Descripción |
|---------|-------------|-------------|
| `theme.json` | sí | Colores y rutas |
| `preview.png` | recomendado | 320×240, vista en el selector |
| `background.png` | opcional | 640×480, fondo del menú |
| `font.ttf` | no usado | Las fuentes Onion rompen el menú; MinUI solo aplica colores y `background.png` |

Ejemplo `theme.json`:

```json
{
  "name": "Mi tema",
  "color_text": "#ffffff",
  "color_text_dim": "#999999",
  "color_pill_selected": "#ffffff",
  "color_pill_unselected": "#262626",
  "color_pill_menu": "#000000",
  "color_button": "#ffffff",
  "color_bar": "#ffffff",
  "color_bar_bg": "#000000",
  "color_bar_bg_menu": "#262626",
  "background": "background.png",
  "font": ""
}
```

## Importar un tema de Onion

Muchos temas de [OnionUI/Themes](https://github.com/OnionUI/Themes) traen un fondo de **640×480** (`skin/background.png`) compatible con MinUI. No se pueden usar los ~80 PNG del `skin/` de Onion (otro launcher), pero sí el aspecto general:

```bash
python3 minui/scripts/importar_tema_onion.py "~/Downloads/Mi-Tema-Onion.zip"
```

Crea `skeleton/Temas/<nombre>/` con `theme.json`, `background.png`, `preview.png` y la fuente si el zip la incluye. Copia la carpeta a `Temas/` en la SD.

Ejemplo incluido: **DS** (UnBurn), importado desde tema Onion.

En este fork solo se incluyen **PlayStation** y **DS** además del tema **Clásico** integrado. Para regenerar desde Onion:

```bash
python3 minui/scripts/importar_tema_onion.py "~/Downloads/PSX Theme by Oclain.zip" PlayStation
python3 minui/scripts/importar_tema_onion.py "~/Downloads/DS XS by UnBurn.zip" DS
```

Los sprites del sistema (`assets@2x.png`) siguen siendo los de MinUI; el tema recolorea las píldoras y la barra superior.

## Animaciones del tema PS2

Todo va en RGB565 crudo (sin PNG): decodificar PNG en la consola era lo que la hacía lenta.

- **Fondo del menú**: `anim.rgb` = fotogramas de `anim_w`×`anim_h` uno tras otro, dibujados en
  (`anim_x`,`anim_y`) sobre `background.png` (negro). `theme.json`: `anim_frames`, `anim_fps`,
  `anim_loop_from`, `anim_max_hz` (tope de redibujados/s). Los fotogramas `0..loop_from-1` se ven
  una sola vez al abrir el menú (cada vez que se vuelve a él) y luego se repite `loop_from..N-1`.
- **Arranque de juego**: `boot_raw` (640×480), `boot_frames`, `boot_fps`, `boot_audio` (wav). Solo al
  abrir un juego de forma normal (no con SEGUIR ni al reanudar tras apagar); cualquier botón la salta.
  Reemplaza la cortinilla de carga (`cargando.png`): corre en un hilo mientras minarch carga el núcleo y el
  juego, y se espera a que termine antes de iniciar el audio del emulador. minui crea
  `/tmp/normal_launch.txt` (solo si el tema tiene animación) y minarch lo consume; los `launch.sh` de PS y
  arcade no muestran `cargando.png` cuando ese archivo existe.
- Se generan con `scripts/ps2_animaciones.sh <fondo.mov> <arranque.mp4>`.
- **Música de fondo del menú** (solo minui): `music` (wav 44,1 kHz estéreo s16, en bucle, empieza en un punto al azar con
  fundido de entrada) y `music_volume` (0–100). Para silenciarla sin cambiar de tema crea `.userdata/miyoomini/sin-musica.txt`.
