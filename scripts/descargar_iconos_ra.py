#!/usr/bin/env python3
"""Descarga iconos de juegos desde RetroAchievements y los sube como
miniaturas Roms/<sistema>/.res/<nombre-de-rom>.png directamente a la Miyoo
por Wi-Fi (mismo mecanismo que desplegar_temas.py). Es el formato que ya lee
el navegador de MinUI, incluida la cuadrícula de iconos del tema PlayStation.

Necesita una API key gratuita de tu cuenta de RetroAchievements:
  https://retroachievements.org/controlpanel.php -> "Keys"

IMPORTANTE: corre este script tú mismo, en tu propia máquina. La API de
RetroAchievements rechaza peticiones sin un User-Agent de navegador normal
(protección anti-bot de Cloudflare) — este script ya lo manda, pero un
agente automatizado no debería simular ser un navegador por ti.

Uso:
  python3 scripts/descargar_iconos_ra.py <API_KEY> "<carpeta en Roms/>" [IP] [--umbral 0.6]

Ejemplo (coincide con las carpetas reales de esta consola):
  python3 scripts/descargar_iconos_ra.py TU_KEY "PlayStation (PS)"
  python3 scripts/descargar_iconos_ra.py TU_KEY "Game Boy (GB)" 192.168.0.108

Empareja cada ROM con el juego de RetroAchievements más parecido por nombre
(difflib) y sube su ImageIcon a Roms/<carpeta>/.res/<rom>.png. No
sobreescribe miniaturas que ya existan salvo --forzar.
"""

import argparse
import difflib
import hashlib
import json
import re
import sys
import urllib.parse
import urllib.request

RA_API = "https://retroachievements.org/API"
RA_IMG = "https://retroachievements.org"
USER_AGENT = "Mozilla/5.0 (compatible; miyoo-mini-plus theme tool)"

# Nombre de carpeta Roms/<...> tal como aparece de verdad en la SD -> ID de
# consola RA. Ampliar según haga falta (ver API_GetConsoleIDs.php).
CONSOLE_IDS = {
    "PlayStation (PS)": 12,
    "Game Boy (GB)": 4,
    "Game Boy Color (GBC)": 6,
    "Game Boy Advance (GBA)": 5,
    "Super Nintendo (SFC)": 3,
    "NES (FC)": 7,
    "Sega 32X (MD)": 1,
    "Master System (SMS)": 11,
    "Neo Geo (NEOGEO)": 28,  # ajusta si tu carpeta es NGPC u otra variante
    "Arcade CPS1 (CPS1)": 27,
    "Arcade CPS2 (CPS2)": 27,
    "Arcade CPS3 (CPS3)": 27,
    "Arcade Clásicos (FBN)": 27,
}


def clean_title(name: str) -> str:
    name = re.sub(r"\.[^.]+$", "", name)
    name = re.sub(r"\([^)]*\)|\[[^\]]*\]", "", name)
    name = re.sub(r"\s+", " ", name).strip()
    return name


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def ra_get(endpoint: str, api_key: str, **params) -> dict:
    params["y"] = api_key
    url = f"{RA_API}/{endpoint}?" + urllib.parse.urlencode(params)
    return json.loads(fetch(url).decode())


class FileBrowser:
    def __init__(self, ip: str):
        self.base = f"http://{ip}"
        self.token = self._pedir(
            "POST", "login",
            json.dumps({"username": "", "password": "", "recaptcha": ""}).encode(),
        ).decode()

    def _pedir(self, metodo, ruta, datos=None, **params):
        url = f"{self.base}/api/{ruta}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        r = urllib.request.Request(url, data=datos, method=metodo)
        r.add_header("X-Auth", self.token) if hasattr(self, "token") else None
        with urllib.request.urlopen(r, timeout=60) as x:
            return x.read()

    @staticmethod
    def _q(ruta):
        return urllib.parse.quote(ruta, safe="/")

    def listar(self, carpeta: str):
        data = self._pedir("GET", f"resources/{self._q(carpeta)}")
        return json.loads(data).get("items", [])

    def existe(self, ruta: str) -> bool:
        try:
            self._pedir("GET", f"resources/{self._q(ruta)}")
            return True
        except Exception:
            return False

    def subir(self, datos: bytes, destino: str):
        tmp = destino + ".nuevo"
        self._pedir("POST", f"resources/{self._q(tmp)}", datos, override="true")
        self._pedir(
            "PATCH", f"resources/{self._q(tmp)}", None,
            action="rename", destination="/" + destino, override="true",
        )
        remoto = self._pedir("GET", f"raw/{self._q(destino)}")
        if hashlib.md5(remoto).digest() != hashlib.md5(datos).digest():
            raise SystemExit(f"ERROR verificando {destino}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("api_key")
    p.add_argument("carpeta", help='Nombre de la carpeta dentro de Roms/, p. ej. "PlayStation (PS)"')
    p.add_argument("ip", nargs="?", default="192.168.0.108")
    p.add_argument("--umbral", type=float, default=0.55, help="Similitud mínima 0-1 (default 0.55)")
    p.add_argument("--forzar", action="store_true", help="Sobreescribe miniaturas ya existentes")
    args = p.parse_args()

    console_id = CONSOLE_IDS.get(args.carpeta)
    if not console_id:
        sys.exit(
            f"No tengo el ID de RetroAchievements para '{args.carpeta}'.\n"
            f"Agrégalo a CONSOLE_IDS en este script (consulta API_GetConsoleIDs.php)."
        )

    fb = FileBrowser(args.ip)
    ruta_rel = f"Roms/{args.carpeta}"
    items = fb.listar(ruta_rel)
    roms = [it["name"] for it in items if not it.get("isDir")]
    print(f"{len(roms)} ROMs en {ruta_rel}")

    print(f"Consultando catálogo de RetroAchievements (consola {console_id})…")
    games = ra_get("API_GetGameList.php", args.api_key, i=console_id)
    if not isinstance(games, list):
        sys.exit(f"Respuesta inesperada de la API: {games}")
    print(f"{len(games)} juegos en el catálogo")

    names = [g["Title"] for g in games]
    by_name = {g["Title"]: g for g in games}

    for rom in sorted(roms):
        dest = f"{ruta_rel}/.res/{rom}.png"
        if not args.forzar and fb.existe(dest):
            print("  (ya existe)", rom)
            continue

        title = clean_title(rom)
        matches = difflib.get_close_matches(title, names, n=1, cutoff=args.umbral)
        if not matches:
            print("  sin coincidencia:", rom, f"({title})")
            continue
        game = by_name[matches[0]]
        if not game.get("ImageIcon"):
            print("  sin icono:", rom)
            continue

        try:
            img_data = fetch(RA_IMG + game["ImageIcon"])
            fb.subir(img_data, dest)
            print(f"  OK  {rom}  ->  {game['Title']}")
        except Exception as e:
            print(f"  ERROR {rom}: {e}")

    print(f"\nListo. Miniaturas subidas a {ruta_rel}/.res/ en {args.ip}")


if __name__ == "__main__":
    main()
