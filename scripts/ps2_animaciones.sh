#!/bin/sh
# Genera las animaciones del tema PS2 a partir de los dos videos originales.
#
# Uso: scripts/ps2_animaciones.sh <video-fondo.mov> <video-arranque.mp4> [video-ambiente.mkv] [carpeta-tema]
#
# Todo se guarda en RGB565 crudo (sin PNG): decodificar PNG en la consola era lo que
# la volvía lenta; leer un bloque y copiarlo a pantalla casi no cuesta CPU.
#
# Fondo del menú -> anim.rgb: fotogramas de 232x232 a 30 fps, uno tras otro. Solo es
#   la zona del anillo; se dibuja en (394,124) de la pantalla sobre un background.png negro
#   (centro en 510,240: a la derecha del centro, como la animación anterior, algo más
#   grande y más abajo).
#   - intro = fotogramas 0..592 (se ve una sola vez al abrir el menú)
#   - bucle = fotogramas 593..1217 (625; el final enlaza con el principio sin fundido,
#             es el único punto del video donde la imagen se repite)
# Arranque de juego -> arranque.rgb (640x480, 20 fps) + arranque.wav.
# Música de fondo del menú -> ambiente.wav (opcional, 3er argumento: video de ambiente; se
#   toma 1 min del minuto 2 y los últimos 4 s se funden con el inicio para que el bucle no corte).
set -e
FONDO="$1"; ARRANQUE="$2"; AMBIENTE="$3"
TEMA="${4:-$(dirname "$0")/../skeleton/Temas/PS2}"
[ -f "$FONDO" ] && [ -f "$ARRANQUE" ] || { echo "uso: $0 fondo.mov arranque.mp4 [tema]"; exit 1; }
mkdir -p "$TEMA"

# recorte cuadrado centrado en el anillo (4K) -> 232x232
ffmpeg -v error -y -i "$FONDO" \
  -vf "trim=start_frame=0:end_frame=1218,setpts=PTS-STARTPTS,crop=1472:1472:984:472,scale=232:232:flags=lanczos,fps=30" \
  -f rawvideo -pix_fmt rgb565le "$TEMA/anim.rgb"
echo "fondo: $(( $(wc -c < "$TEMA/anim.rgb") / (232*232*2) )) fotogramas"

# fondo negro 640x480
ffmpeg -v error -y -f lavfi -i color=c=black:s=640x480 -frames:v 1 "$TEMA/background.png"

# arranque: cubos (0..9 s) + logo (14.4..16.9 s), sin el negro del medio
ffmpeg -v error -y -i "$ARRANQUE" -filter_complex \
  "[0:v]split[a][b];[a]trim=0:9,setpts=PTS-STARTPTS[v1];[b]trim=14.4:16.9,setpts=PTS-STARTPTS[v2];[v1][v2]concat=n=2:v=1,scale=640:480:flags=lanczos,fps=20[v]; \
   [0:a]asplit[c][d];[c]atrim=0:9,asetpts=PTS-STARTPTS[a1];[d]atrim=14.4:16.9,asetpts=PTS-STARTPTS[a2];[a1][a2]concat=n=2:v=0:a=1,aresample=44100,aformat=sample_fmts=s16:channel_layouts=stereo,afade=t=out:st=11:d=0.5[a]" \
  -map "[v]" -f rawvideo -pix_fmt rgb565le "$TEMA/arranque.rgb" -map "[a]" "$TEMA/arranque.wav"
echo "arranque: $(( $(wc -c < "$TEMA/arranque.rgb") / (640*480*2) )) fotogramas"

if [ -n "$AMBIENTE" ] && [ -f "$AMBIENTE" ]; then
  ffmpeg -v error -y -ss 120 -t 64 -i "$AMBIENTE" -vn -filter_complex \
    "[0:a]aresample=44100,aformat=sample_fmts=s16:channel_layouts=stereo,asplit=3[m0][t0][h0]; \
     [m0]atrim=4:60,asetpts=PTS-STARTPTS[m]; [t0]atrim=60:64,asetpts=PTS-STARTPTS[t]; [h0]atrim=0:4,asetpts=PTS-STARTPTS[h]; \
     [t][h]acrossfade=d=4:c1=tri:c2=tri[x]; [m][x]concat=n=2:v=0:a=1,loudnorm=I=-26:TP=-3:LRA=11,aformat=sample_fmts=s16:channel_layouts=stereo:sample_rates=44100" \
    "$TEMA/ambiente.wav"
  echo "ambiente: $(ls -l "$TEMA/ambiente.wav" | awk '{print $5}') bytes"
fi
