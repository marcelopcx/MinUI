#!/bin/sh
# Copia los núcleos arcade de Onion OS a los paks CPS1/CPS2/NEOGEO/FBN/MAME
# de una SD (o de build/EXTRAS). CPS3 usa el núcleo de FBN.pak.
# Uso: scripts/nucleos-arcade.sh /Volumes/MIYOO
set -e
DEST="${1:?indica la raíz de la SD, ej. /Volumes/MIYOO}/Emus/miyoomini"
ONION_URL="https://github.com/OnionUI/Onion/releases/download/latest/Onion-v4.4.0-beta-20260120-07505ea5.zip"
TMP=$(mktemp -d)
echo "Descargando Onion..."
curl -L --http1.1 --retry 5 -o "$TMP/onion.zip" "$ONION_URL"
unzip -q "$TMP/onion.zip" 'RetroArch/retroarch.pak' -d "$TMP"
mkdir -p "$TMP/ra" && (cd "$TMP/ra" && bsdtar xf "$TMP/RetroArch/retroarch.pak" 'RetroArch/.retroarch/cores/*')
C="$TMP/ra/RetroArch/.retroarch/cores"
for p in CPS1:fbalpha2012_cps1 CPS2:fbalpha2012_cps2 NEOGEO:fbalpha2012_neogeo FBN:fbneo MAME:mame2003_plus; do
	cp "$C/${p#*:}_libretro.so" "$DEST/${p%%:*}.pak/"
	echo "OK ${p%%:*}.pak <- ${p#*:}"
done
rm -rf "$TMP"
