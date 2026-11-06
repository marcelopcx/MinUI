#!/usr/bin/env python3
"""miyoo-sync: sincroniza una Miyoo Mini Plus (MinUI) con RomM.

La consola, al encender su Wi-Fi, llama a  GET /sync?ip=<ip>  y luego consulta
GET /estado. El servidor entra al explorador de archivos de la consola
(filebrowser, puerto 80) y:

  * Juegos: descarga a la consola los juegos de la colección "Miyoo" de RomM.
  * Enlaza por nombre los juegos que ya existen en ambos lados (aunque el
    formato sea distinto, p. ej. CHD en la consola y .bin en RomM).
  * Partidas (.sav) y estados (.st0-.st9) de los juegos enlazados: sincronización
    en ambos sentidos. La consola no tiene reloj, así que no se usan fechas: se
    recuerda el hash de la última versión sincronizada (sincronización a 3 bandas).
    Si cambió en los dos lados, la versión de RomM se guarda como copia
    "(servidor <fecha>)" y se usa la de la consola; no se pierde ninguna.

Solo usa la biblioteca estándar de Python.
"""
import hashlib, http.server, json, os, re, threading, time, traceback, unicodedata, uuid
import urllib.error, urllib.parse, urllib.request

ROMM = os.environ.get("ROMM_URL", "http://localhost:8082").rstrip("/")
TOKEN = os.environ["ROMM_TOKEN"]
PUERTO = int(os.environ.get("PORT", "8043"))
COLECCION = os.environ.get("COLECCION", "Miyoo")
DATOS = os.environ.get("DATA_DIR", "/data")
ESTADO_PATH = os.path.join(DATOS, "estado.json")

# plataforma de RomM -> (carpeta de MinUI, etiqueta, núcleo que usa la consola)
PLATAFORMAS = {
    "gb": ("Game Boy (GB)", "GB", "gambatte"),
    "gbc": ("Game Boy Color (GBC)", "GBC", "gambatte"),
    "gba": ("Game Boy Advance (GBA)", "GBA", "gpsp"),
    "nes": ("NES (FC)", "FC", "fceumm"),
    "famicom": ("NES (FC)", "FC", "fceumm"),
    "snes": ("Super Nintendo (SFC)", "SFC", "snes9x2005_plus"),
    "sfam": ("Super Nintendo (SFC)", "SFC", "snes9x2005_plus"),
    "genesis": ("Sega Genesis (MD)", "MD", "picodrive"),
    "megadrive": ("Sega Genesis (MD)", "MD", "picodrive"),
    "sms": ("Master System (SMS)", "SMS", "picodrive"),
    "gamegear": ("Sega Game Gear (GG)", "GG", "picodrive"),
    "psx": ("PlayStation (PS)", "PS", "pcsx_rearmed"),
    "tg16": ("TurboGrafx-16 (PCE)", "PCE", "mednafen_pce_fast"),
    "pce": ("TurboGrafx-16 (PCE)", "PCE", "mednafen_pce_fast"),
    "ngpc": ("Neo Geo Pocket Color (NGPC)", "NGPC", "race"),
    "virtualboy": ("Virtual Boy (VB)", "VB", "mednafen_vb"),
}

ejecucion = {"corriendo": False, "resumen": "Sin sincronizar todavía", "ip": None}
candado = threading.Lock()


def log(*a):
    print(time.strftime("%Y-%m-%d %H:%M:%S"), *a, flush=True)


def md5(b):
    return hashlib.md5(b).hexdigest()


def normalizar(nombre):
    """Título comparable: sin etiquetas (USA), sin tildes, sin signos."""
    n = os.path.splitext(nombre)[0]
    n = re.sub(r"[\(\[].*?[\)\]]", "", n)
    n = unicodedata.normalize("NFKD", n).encode("ascii", "ignore").decode().lower()
    n = re.sub(r"\b(the|el|la|los|las)\b", "", n)
    return re.sub(r"[^a-z0-9]", "", n)


def mismo_juego(a, b):
    a, b = normalizar(a), normalizar(b)
    if not a or not b:
        return False
    corto, largo = sorted((a, b), key=len)
    return corto == largo or (len(corto) >= 8 and corto in largo)


def nombre_fat(n):
    return re.sub(r'[\\/:*?"<>|]', "-", n).strip()


# --------------------------------------------------------------------- HTTP ---
def pedir(url, metodo="GET", datos=None, cabeceras=None, tiempo=60, intentos=3):
    ultimo = None
    for i in range(intentos):
        try:
            req = urllib.request.Request(url, data=datos, method=metodo, headers=cabeceras or {})
            with urllib.request.urlopen(req, timeout=tiempo) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise
            ultimo = e
        except Exception as e:  # red de la consola inestable
            ultimo = e
        time.sleep(2 * (i + 1))
    raise ultimo


def multipart(campo, nombre, contenido):
    frontera = uuid.uuid4().hex
    cuerpo = (f"--{frontera}\r\nContent-Disposition: form-data; name=\"{campo}\"; "
              f"filename=\"{nombre}\"\r\nContent-Type: application/octet-stream\r\n\r\n").encode() \
        + contenido + f"\r\n--{frontera}--\r\n".encode()
    return cuerpo, {"Content-Type": f"multipart/form-data; boundary={frontera}"}


class RomM:
    def __init__(self):
        self.cab = {"Authorization": f"Bearer {TOKEN}"}

    def get(self, ruta, **q):
        url = f"{ROMM}{ruta}" + (("?" + urllib.parse.urlencode(q, doseq=True)) if q else "")
        return json.loads(pedir(url, cabeceras=self.cab))

    def bytes(self, ruta):
        return pedir(f"{ROMM}{ruta}", cabeceras=self.cab, tiempo=600)

    def roms(self):
        d = self.get("/api/roms", limit=10000)
        return d["items"] if isinstance(d, dict) else d

    def coleccion(self):
        for c in self.get("/api/collections"):
            if c["name"].lower() == COLECCION.lower():
                return c
        cuerpo = (f"--x\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n{COLECCION}\r\n"
                  f"--x\r\nContent-Disposition: form-data; name=\"description\"\r\n\r\n"
                  f"Juegos que se descargan a la Miyoo Mini Plus\r\n--x--\r\n").encode()
        pedir(f"{ROMM}/api/collections", "POST", cuerpo,
              {**self.cab, "Content-Type": "multipart/form-data; boundary=x"})
        log(f"Colección '{COLECCION}' creada en RomM")
        return self.coleccion()

    def subir(self, tipo, rom_id, nucleo, nombre, contenido):
        campo = "saveFile" if tipo == "saves" else "stateFile"
        cuerpo, cab = multipart(campo, nombre, contenido)
        q = urllib.parse.urlencode({"rom_id": rom_id, "emulator": nucleo})
        pedir(f"{ROMM}/api/{tipo}?{q}", "POST", cuerpo, {**self.cab, **cab}, tiempo=300)

    def ultimo(self, tipo, rom_id, nombre):
        """Versión más reciente en RomM de un archivo de la consola (o None)."""
        mejores = [s for s in self.get(f"/api/{tipo}", rom_id=rom_id) if s.get("file_name") == nombre]
        if not mejores:
            return None
        mejores.sort(key=lambda s: (s.get("updated_at") or "", s["id"]))
        return mejores[-1]


class Consola:
    """Explorador de archivos (filebrowser) de la Miyoo."""

    def __init__(self, ip):
        self.base = f"http://{ip}"
        self.tok = pedir(f"{self.base}/api/login", "POST",
                         b'{"username":"","password":"","recaptcha":""}', tiempo=20).decode()
        self.cab = {"X-Auth": self.tok}

    def q(self, ruta):
        return urllib.parse.quote(ruta.strip("/"), safe="/")

    def listar(self, ruta):
        try:
            d = json.loads(pedir(f"{self.base}/api/resources/{self.q(ruta)}/", cabeceras=self.cab))
            return d.get("items") or []
        except urllib.error.HTTPError:
            return []

    def leer(self, ruta):
        try:
            return pedir(f"{self.base}/api/raw/{self.q(ruta)}", cabeceras=self.cab, tiempo=300)
        except urllib.error.HTTPError:
            return None

    def crear_carpeta(self, ruta):
        try:
            pedir(f"{self.base}/api/resources/{self.q(ruta)}/?override=false", "POST", b"", self.cab)
        except urllib.error.HTTPError:
            pass

    def escribir(self, ruta, contenido):
        carpeta, nombre = os.path.split(ruta.strip("/"))
        self.crear_carpeta(carpeta)
        tmp = f"{carpeta}/.subiendo-{uuid.uuid4().hex[:8]}"
        pedir(f"{self.base}/api/resources/{self.q(tmp)}?override=true", "POST", contenido, self.cab, tiempo=3600)
        destino = urllib.parse.quote("/" + ruta.strip("/"), safe="")
        pedir(f"{self.base}/api/resources/{self.q(tmp)}?action=rename&destination={destino}&override=true",
              "PATCH", b"", self.cab)
        if md5(self.leer(ruta) or b"") != md5(contenido):
            raise IOError(f"verificación fallida: {ruta}")


# ------------------------------------------------------------------- estado ---
def cargar_estado():
    try:
        with open(ESTADO_PATH) as f:
            return json.load(f)
    except FileNotFoundError:
        return {"enlaces": {}, "hashes": {}}


def guardar_estado(e):
    os.makedirs(DATOS, exist_ok=True)
    tmp = ESTADO_PATH + ".tmp"
    with open(tmp, "w") as f:
        json.dump(e, f, indent=1, ensure_ascii=False)
    os.replace(tmp, ESTADO_PATH)


# ----------------------------------------------------------- sincronización ---
def sincronizar(ip):
    romm, miyoo, est = RomM(), Consola(ip), cargar_estado()
    r = {"juegos": 0, "subidas": 0, "bajadas": 0, "conflictos": 0, "errores": 0}

    roms = romm.roms()
    en_coleccion = set(romm.coleccion().get("rom_ids") or [])

    # 1) enlazar juegos y descargar los de la colección
    for rom in roms:
        plat = PLATAFORMAS.get(rom.get("platform_slug"))
        if not plat:
            continue
        carpeta, tag, nucleo = plat
        locales = [i["name"] for i in miyoo.listar(f"Roms/{carpeta}")
                   if not i.get("isDir") and not i["name"].startswith(".") and i["name"] != "map.txt"]
        ya = next((n for n in locales if mismo_juego(n, rom.get("name") or rom["fs_name"])
                   or mismo_juego(n, rom["fs_name"])), None)
        if not ya and rom["id"] in en_coleccion:
            ext = os.path.splitext(rom["fs_name"])[1].lower()
            ya = nombre_fat(rom.get("name") or os.path.splitext(rom["fs_name"])[0]) + ext
            log(f"descargando {rom['fs_name']} -> {carpeta}/{ya}")
            contenido = romm.bytes(f"/api/roms/{rom['id']}/content/{urllib.parse.quote(rom['fs_name'])}")
            miyoo.escribir(f"Roms/{carpeta}/{ya}", contenido)
            r["juegos"] += 1
        if ya:
            est["enlaces"][f"Roms/{carpeta}/{ya}"] = {"rom_id": rom["id"], "tag": tag, "nucleo": nucleo}
    guardar_estado(est)

    # 2) partidas y estados de los juegos enlazados
    for ruta_rom, info in est["enlaces"].items():
        archivo = os.path.basename(ruta_rom)
        tag, nucleo, rom_id = info["tag"], info["nucleo"], info["rom_id"]
        candidatos = [("saves", f"Saves/{tag}/{archivo}.sav")]
        carpeta_estados = f".userdata/shared/{tag}-{nucleo}"
        candidatos += [("states", f"{carpeta_estados}/{archivo}.st{n}") for n in range(10)]
        nombres_estado = {i["name"] for i in miyoo.listar(carpeta_estados)}
        for tipo, ruta in candidatos:
            nombre = os.path.basename(ruta)
            local = None
            if tipo == "saves" or nombre in nombres_estado:
                local = miyoo.leer(ruta)
            remoto_info = romm.ultimo(tipo, rom_id, nombre)
            if local is None and remoto_info is None:
                continue
            try:
                hl = md5(local) if local is not None else None
                hr = (remoto_info.get("content_hash") or None) if remoto_info else None
                if remoto_info and not hr:   # RomM sin hash: se descarga para calcularlo
                    hr = md5(romm.bytes(f"/api/{tipo}/{remoto_info['id']}/content"))
                ant = est["hashes"].get(ruta)
                if hl == hr:
                    pass
                elif hr is None or (hl is not None and hr == ant):      # cambió solo en la consola
                    romm.subir(tipo, rom_id, nucleo, nombre, local); r["subidas"] += 1; hr = hl
                    log(f"subida {ruta}")
                elif hl is None or hl == ant:                            # cambió solo en RomM
                    remoto = romm.bytes(f"/api/{tipo}/{remoto_info['id']}/content")
                    miyoo.escribir(ruta, remoto); r["bajadas"] += 1; hl = hr = md5(remoto)
                    log(f"bajada {ruta}")
                else:                                                    # cambió en los dos
                    # RomM sobrescribe por nombre: primero se guarda su versión aparte
                    base, ext = nombre.rsplit(".", 1)
                    respaldo = f"{base} (servidor {time.strftime('%Y-%m-%d %H%M')}).{ext}"
                    romm.subir(tipo, rom_id, nucleo, respaldo,
                               romm.bytes(f"/api/{tipo}/{remoto_info['id']}/content"))
                    romm.subir(tipo, rom_id, nucleo, nombre, local); r["conflictos"] += 1; hr = hl
                    log(f"CONFLICTO {ruta}: versión del servidor guardada como '{respaldo}', "
                        "se usa la de la consola")
                if hl == hr and hl:
                    est["hashes"][ruta] = hl
            except Exception:
                r["errores"] += 1
                log(f"error con {ruta}\n{traceback.format_exc()}")
        guardar_estado(est)

    partes = [f"{r['juegos']} juegos nuevos", f"{r['subidas']} subidas", f"{r['bajadas']} bajadas"]
    if r["conflictos"]:
        partes.append(f"{r['conflictos']} conflictos")
    if r["errores"]:
        partes.append(f"{r['errores']} errores")
    return ", ".join(partes)


def trabajo(ip):
    try:
        log(f"sincronizando con la consola en {ip}")
        resumen = sincronizar(ip)
    except Exception as e:
        log(traceback.format_exc())
        resumen = f"Error: {e}"[:80]
    with candado:
        ejecucion.update(corriendo=False, resumen=resumen)
    log("terminado:", resumen)


class Servidor(http.server.BaseHTTPRequestHandler):
    def responder(self, texto, codigo=200):
        b = texto.encode()
        self.send_response(codigo)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/sync":
            ip = (q.get("ip") or [self.client_address[0]])[0]
            if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", ip):
                return self.responder("ip no válida\n", 400)
            with candado:
                if not ejecucion["corriendo"]:
                    ejecucion.update(corriendo=True, ip=ip, resumen="Sincronizando...")
                    threading.Thread(target=trabajo, args=(ip,), daemon=True).start()
            return self.responder("OK\n")
        if url.path == "/estado":
            with candado:
                return self.responder(("CORRIENDO" if ejecucion["corriendo"] else "LISTO") + "\n"
                                      + ejecucion["resumen"] + "\n")
        self.responder("miyoo-sync\n")

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    log(f"miyoo-sync escuchando en el puerto {PUERTO}, RomM en {ROMM}")
    http.server.ThreadingHTTPServer(("0.0.0.0", PUERTO), Servidor).serve_forever()
