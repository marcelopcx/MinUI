#!/usr/bin/env python3
"""miyoo-sync: sincroniza una Miyoo Mini Plus (MinUI) con RomM.

En casa la consola llama a GET /sync y el servidor entra a su explorador de
archivos. Desde fuera, la consola habla con /remoto (clave SYNC_TOKEN): manda
la lista de sus archivos y ella misma sube y baja lo que haga falta. El
servidor no intenta entrar a una IP que no está en la red de casa.

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
import hashlib, hmac, http.server, json, os, re, threading, time, traceback, unicodedata, uuid
import urllib.error, urllib.parse, urllib.request

ROMM = os.environ.get("ROMM_URL", "http://localhost:8082").rstrip("/")
TOKEN = os.environ["ROMM_TOKEN"]
SYNC_TOKEN = os.environ.get("SYNC_TOKEN", "")
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


def es_red_local(ip):
    """La consola en casa. 127.0.0.1 y la VPN (100.x) no cuentan."""
    partes = ip.split(".")
    if len(partes) != 4 or not all(p.isdigit() for p in partes):
        return False
    a, b = int(partes[0]), int(partes[1])
    if a == 10 or (a == 192 and b == 168):
        return True
    return a == 172 and 16 <= b <= 31


def decidir(local, remoto, anterior):
    """Qué hacer con una partida. local/remoto/anterior son md5 o None."""
    if local == remoto:
        return "igual"
    if remoto is None or (local is not None and remoto == anterior):
        return "subir"
    if local is None or local == anterior:
        return "bajar"
    return "conflicto"


def ruta_permitida(ruta):
    ruta = (ruta or "").replace("\\", "/").strip("/")
    if not ruta or ".." in ruta.split("/"):
        return None
    if ruta.startswith(("Roms/", "Saves/", ".userdata/shared/")):
        return ruta
    return None


def hash_remoto(romm, tipo, info):
    if not info:
        return None
    hecho = info.get("content_hash") or None
    if hecho:
        return hecho
    return md5(romm.bytes(f"/api/{tipo}/{info['id']}/content"))


# El plan de la sincronización remota que está en curso: ruta -> qué hacer.
plan_actual = {}


def construir_plan(texto):
    """Lee el manifiesto de la consola y prepara bajadas y subidas."""
    global plan_actual
    romm, est = RomM(), cargar_estado()
    roms = romm.roms()
    en_coleccion = set(romm.coleccion().get("rom_ids") or [])
    locales, archivos = [], {}
    for linea in texto.splitlines():
        partes = linea.split("\t")
        if len(partes) == 2 and partes[0] == "ROM":
            ruta = ruta_permitida(partes[1])
            if ruta and ruta.startswith("Roms/") and ruta.count("/") == 2:
                locales.append(ruta)
        elif len(partes) == 3 and partes[0] == "FILE":
            ruta = ruta_permitida(partes[1])
            huella = partes[2].strip().lower()
            if ruta and re.fullmatch(r"[0-9a-f]{32}", huella):
                archivos[ruta] = huella

    plan = {}
    for rom in roms:
        plat = PLATAFORMAS.get(rom.get("platform_slug"))
        if not plat:
            continue
        carpeta, tag, nucleo = plat
        prefijo = f"Roms/{carpeta}/"
        nombres = [os.path.basename(p) for p in locales if p.startswith(prefijo)]
        ya = next((n for n in nombres if mismo_juego(n, rom.get("name") or rom["fs_name"])
                   or mismo_juego(n, rom["fs_name"])), None)
        if not ya and rom["id"] in en_coleccion:
            ext = os.path.splitext(rom["fs_name"])[1].lower()
            ya = nombre_fat(rom.get("name") or os.path.splitext(rom["fs_name"])[0]) + ext
            plan[f"{prefijo}{ya}"] = {"op": "juego", "rom_id": rom["id"], "fs_name": rom["fs_name"]}
        if ya:
            est["enlaces"][f"{prefijo}{ya}"] = {"rom_id": rom["id"], "tag": tag, "nucleo": nucleo}
    guardar_estado(est)

    for ruta_rom, info in est["enlaces"].items():
        archivo = os.path.basename(ruta_rom)
        tag, nucleo, rom_id = info["tag"], info["nucleo"], info["rom_id"]
        candidatos = [("saves", f"Saves/{tag}/{archivo}.sav")]
        candidatos += [("states", f".userdata/shared/{tag}-{nucleo}/{archivo}.st{n}") for n in range(10)]
        for tipo, ruta in candidatos:
            if ruta in plan:
                continue
            remoto_info = romm.ultimo(tipo, rom_id, os.path.basename(ruta))
            local = archivos.get(ruta)
            if local is None and remoto_info is None:
                continue
            remoto = hash_remoto(romm, tipo, remoto_info)
            anterior = est["hashes"].get(ruta)
            que = decidir(local, remoto, anterior)
            if que == "igual" and local:
                est["hashes"][ruta] = local
            elif que == "bajar":
                plan[ruta] = {"op": "bajar", "tipo": tipo, "asset_id": remoto_info["id"]}
            elif que in ("subir", "conflicto"):
                plan[ruta] = {"op": "subir", "tipo": tipo, "rom_id": rom_id, "nucleo": nucleo,
                              "nombre": os.path.basename(ruta), "asset_id": (remoto_info or {}).get("id")}
    guardar_estado(est)
    plan_actual = plan
    lineas = []
    for ruta, item in plan.items():
        verbo = {"juego": "JUEGO", "bajar": "BAJAR", "subir": "SUBIR"}[item["op"]]
        lineas.append(f"{verbo}\t{ruta}")
    log(f"plan remoto: {len(lineas)} acciones")
    return "\n".join(lineas) + ("\n" if lineas else "")


def contenido_bajada(ruta):
    item = plan_actual.get(ruta)
    if not item or item["op"] not in ("juego", "bajar"):
        return None, None
    romm = RomM()
    if item["op"] == "juego":
        datos = romm.bytes(f"/api/roms/{item['rom_id']}/content/{urllib.parse.quote(item['fs_name'])}")
    else:
        datos = romm.bytes(f"/api/{item['tipo']}/{item['asset_id']}/content")
    huella = md5(datos)
    item["md5"] = huella
    return datos, huella


def aplicar_subida(ruta, contenido):
    item = plan_actual.get(ruta)
    if not item or item["op"] != "subir":
        return None
    romm, est = RomM(), cargar_estado()
    local = md5(contenido)
    remoto_info = romm.ultimo(item["tipo"], item["rom_id"], item["nombre"])
    remoto = hash_remoto(romm, item["tipo"], remoto_info)
    que = decidir(local, remoto, est["hashes"].get(ruta))
    if que == "igual":
        est["hashes"][ruta] = local
        guardar_estado(est)
        return "igual"
    if que == "bajar":
        return "BAJAR"
    if que == "conflicto" and remoto_info:
        base, ext = item["nombre"].rsplit(".", 1)
        respaldo = f"{base} (servidor {time.strftime('%Y-%m-%d %H%M')}).{ext}"
        romm.subir(item["tipo"], item["rom_id"], item["nucleo"], respaldo,
                   romm.bytes(f"/api/{item['tipo']}/{remoto_info['id']}/content"))
        log(f"CONFLICTO {ruta}: respaldo '{respaldo}'")
    if que in ("subir", "conflicto"):
        romm.subir(item["tipo"], item["rom_id"], item["nucleo"], item["nombre"], contenido)
        log(f"subida remota {ruta}")
    est["hashes"][ruta] = local
    guardar_estado(est)
    return "conflicto" if que == "conflicto" else "subida"


def confirmar_bajada(ruta, huella):
    item = plan_actual.get(ruta)
    if not item or item.get("md5") != huella:
        return False
    if item["op"] == "bajar":
        est = cargar_estado()
        est["hashes"][ruta] = huella
        guardar_estado(est)
    return True


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

    def responder_bytes(self, datos, extra=None):
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(datos)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(datos)

    def cuerpo(self, maximo):
        try:
            n = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            n = -1
        if n < 0 or n > maximo:
            return None
        return self.rfile.read(n)

    def autorizado(self):
        if not SYNC_TOKEN:
            self.responder("sincronización remota no configurada\n", 503)
            return False
        dado = self.headers.get("X-Sync-Token", "")
        if not hmac.compare_digest(dado, SYNC_TOKEN):
            self.responder("clave incorrecta\n", 401)
            return False
        return True

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/sync":
            # Solo la red de casa, y solo hacia quien llama: no se acepta una IP
            # arbitraria (desde internet eso sería un salto a otra máquina).
            ip = self.client_address[0]
            if not es_red_local(ip):
                return self.responder("esta sincronización solo funciona en casa\n", 403)
            with candado:
                if not ejecucion["corriendo"]:
                    ejecucion.update(corriendo=True, ip=ip, resumen="Sincronizando...")
                    threading.Thread(target=trabajo, args=(ip,), daemon=True).start()
            return self.responder("OK\n")
        if url.path == "/estado":
            with candado:
                return self.responder(("CORRIENDO" if ejecucion["corriendo"] else "LISTO") + "\n"
                                      + ejecucion["resumen"] + "\n")
        if url.path == "/remoto/ping":
            if not self.autorizado():
                return
            return self.responder("OK\n")
        if url.path == "/remoto/bajar":
            if not self.autorizado():
                return
            ruta = ruta_permitida((q.get("ruta") or [""])[0])
            if not ruta:
                log(f"ruta rechazada en bajar: {(q.get('ruta') or [''])[0][:160]!r}")
                return self.responder("ruta no válida\n", 400)
            try:
                datos, huella = contenido_bajada(ruta)
            except Exception as e:
                log(f"bajada remota {ruta}\n{traceback.format_exc()}")
                return self.responder(f"Error: {e}"[:200], 500)
            if datos is None:
                return self.responder("no está en el plan\n", 404)
            return self.responder_bytes(datos, {"X-Content-MD5": huella})
        self.responder("miyoo-sync\n")

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/remoto/plan":
            if not self.autorizado():
                return
            cuerpo = self.cuerpo(2 << 20)
            if cuerpo is None:
                return self.responder("manifiesto demasiado grande\n", 413)
            try:
                return self.responder(construir_plan(cuerpo.decode("utf-8", "replace")))
            except Exception as e:
                log(traceback.format_exc())
                return self.responder(f"Error: {e}"[:200], 500)
        if url.path == "/remoto/confirmar":
            if not self.autorizado():
                return
            cuerpo = self.cuerpo(4096)
            if not cuerpo or b"\t" not in cuerpo:
                return self.responder("falta la ruta\n", 400)
            ruta, huella = cuerpo.decode("utf-8", "replace").split("\t", 1)
            ruta = ruta_permitida(ruta)
            if ruta and confirmar_bajada(ruta, huella.strip().lower()):
                return self.responder("OK\n")
            return self.responder("no coincide\n", 409)
        if url.path == "/remoto/subir":
            return self._subir(q)
        self.responder("miyoo-sync\n", 404)

    def do_PUT(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/remoto/subir":
            return self._subir(urllib.parse.parse_qs(url.query))
        self.responder("miyoo-sync\n", 404)

    def _subir(self, q):
        if not self.autorizado():
            return
        ruta = ruta_permitida((q.get("ruta") or [""])[0])
        if not ruta:
            log(f"ruta rechazada en subir: {(q.get('ruta') or [''])[0][:160]!r}")
            return self.responder("ruta no válida\n", 400)
        contenido = self.cuerpo(80 << 20)
        if contenido is None:
            return self.responder("archivo demasiado grande\n", 413)
        try:
            resultado = aplicar_subida(ruta, contenido)
        except Exception as e:
            log(f"subida remota {ruta}\n{traceback.format_exc()}")
            return self.responder(f"Error: {e}"[:200], 500)
        if resultado is None:
            return self.responder("no está en el plan\n", 404)
        self.responder(resultado + "\n")

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    log(f"miyoo-sync escuchando en el puerto {PUERTO}, RomM en {ROMM}")
    http.server.ThreadingHTTPServer(("0.0.0.0", PUERTO), Servidor).serve_forever()
