#!/usr/bin/env python3
"""
Convierte un .zip de tema Onion (config.json + skin/) a carpeta MinUI en Temas/.

Uso:
  python3 importar_tema_onion.py "/ruta/tema.zip" [carpeta_destino]
  python3 importar_tema_onion.py "/ruta/3-pack.zip" PICO-8 --entry "PICO-FANTASY by tenlevels"

No replica toda la UI de Onion; usa background, fuente de lista y colores del config.json.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit("pip install pillow")

ROOT = Path(__file__).resolve().parent.parent / "skeleton" / "Temas"


def slugify(name: str) -> str:
    s = re.sub(r"[^\w\s-]", "", name, flags=re.UNICODE)
    s = re.sub(r"[-\s]+", "-", s.strip())
    return s[:48] or "Tema-Onion"


def first_hex(config: dict, *paths: str, default: str = "") -> str:
    cur = config
    for key in paths:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    if isinstance(cur, str) and cur.startswith("#"):
        return cur.lower()
    return default


def hex_to_rgb(h: str) -> tuple[int, int, int]:
    h = h.lstrip("#")
    if len(h) != 6:
        return 0, 0, 0
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def rgb_to_hex(r: int, g: int, b: int) -> str:
    return f"#{r:02x}{g:02x}{b:02x}"


def dim_hex(hex_c: str, factor: float = 0.55) -> str:
    r, g, b = hex_to_rgb(hex_c)
    return rgb_to_hex(int(r * factor), int(g * factor), int(b * factor))


def background_is_light(img: Image.Image) -> bool:
    small = img.convert("RGB").resize((80, 60))
    pixels = list(small.getdata())
    avg = sum(sum(p) for p in pixels) / (len(pixels) * 3)
    return avg > 140


def palette_for_bg(light: bool) -> dict:
    if light:
        return {
            "color_text": "#1a1a28",
            "color_text_dim": "#5a5a6a",
            "color_pill_selected": "#ffffff",
            "color_pill_unselected": "#b8b8c8",
            "color_pill_menu": "#787888",
            "color_button": "#1a1a28",
            "color_bar": "#3a3a4a",
            "color_bar_bg": "#d8d8e8",
            "color_bar_bg_menu": "#a8a8b8",
        }
    return {
        "color_text": "#e8e8f0",
        "color_text_dim": "#a0a0b0",
        "color_pill_selected": "#ffffff",
        "color_pill_unselected": "#2a2a3a",
        "color_pill_menu": "#101018",
        "color_button": "#e8e8f0",
        "color_bar": "#c0c8e0",
        "color_bar_bg": "#1a1a28",
        "color_bar_bg_menu": "#2a2a3a",
    }


def sample_bg_color(img: Image.Image) -> str:
    small = img.convert("RGB").resize((64, 48))
    pixels = list(small.getdata())
    r = sum(p[0] for p in pixels) // len(pixels)
    g = sum(p[1] for p in pixels) // len(pixels)
    b = sum(p[2] for p in pixels) // len(pixels)
    # oscurecer para contraste con texto claro
    return rgb_to_hex(max(0, int(r * 0.45)), max(0, int(g * 0.45)), max(0, int(b * 0.45)))


def find_config_prefix(namelist: list[str], entry: str | None) -> str:
    configs = [n for n in namelist if n.endswith("config.json")]
    if not configs:
        raise SystemExit("No se encontró config.json en el zip")
    if entry:
        entry = entry.rstrip("/") + "/"
        for c in configs:
            if c.startswith(entry):
                return c[: -len("config.json")]
        raise SystemExit(f"No hay config.json bajo entrada: {entry}")
    if len(configs) == 1:
        return configs[0][: -len("config.json")]
    names = "\n".join(c[: c.rfind("config.json")] for c in configs)
    raise SystemExit(f"Varios temas en el zip; usa --entry:\n{names}")


def import_zip(
    zip_path: Path,
    dest: Path,
    *,
    entry: str | None = None,
    use_background_image: bool = True,
    include_font: bool = True,
) -> None:
    with zipfile.ZipFile(zip_path) as zf:
        prefix = find_config_prefix(zf.namelist(), entry)
        config = json.loads(zf.read(prefix + "config.json").decode("utf-8"))
        display_name = config.get("name") or zip_path.stem
        author = config.get("author", "Onion")

        dest.mkdir(parents=True, exist_ok=True)

        bg_file = ""
        color_background = "#141414"
        bg_light = False

        bg_name = prefix + "skin/background.png"
        if bg_name in zf.namelist():
            data = zf.read(bg_name)
            bg_path = dest / "background.png"
            bg_path.write_bytes(data)
            bg = Image.open(bg_path)
            if bg.size != (640, 480):
                bg = bg.resize((640, 480), Image.Resampling.LANCZOS)
                bg.save(bg_path)
            color_background = sample_bg_color(bg)
            bg_light = background_is_light(bg)
            if use_background_image:
                bg_file = "background.png"

        text = first_hex(config, "list", "color", default="#ffffff")
        text_dim = first_hex(config, "grid", "color", default=dim_hex(text, 0.7))
        if text.lower() in ("#000000", "#000") or text_dim.lower() in ("#000000", "#000"):
            pal = palette_for_bg(bg_light)
            text = pal["color_text"]
            text_dim = pal["color_text_dim"]

        pill_sel = first_hex(config, "grid", "selectedcolor", default="#ffffff")
        text_rgb = hex_to_rgb(text) if text else (255, 255, 255)
        sel_rgb = hex_to_rgb(pill_sel) if pill_sel else (255, 255, 255)
        if sum(abs(a - b) for a, b in zip(text_rgb, sel_rgb)) < 80:
            pill_sel = "#ffffff"
        pill_unsel = dim_hex(text_dim, 0.4) if text_dim else "#262626"
        pill_menu = dim_hex(text, 0.25) if text else "#000000"
        bar = first_hex(config, "currentpage", "color", default=text)
        bar_bg = color_background
        bar_menu = dim_hex(text, 0.35) if text else "#262626"

        font_file = ""
        if include_font:
            list_font = (config.get("list") or {}).get("font") or (config.get("title") or {}).get("font")
            if list_font:
                fn = prefix + list_font
                if fn in zf.namelist():
                    ext = Path(list_font).suffix.lower() or ".ttf"
                    font_file = "font" + ext
                    (dest / font_file).write_bytes(zf.read(fn))
        else:
            for old_font in dest.glob("font.*"):
                old_font.unlink(missing_ok=True)

        if bg_file and bg_light:
            pill_sel = "#f0d030"
            pill_unsel = "#b8b8c0"
            text = "#ffffff"
            text_dim = first_hex(config, "grid", "color", default="#949494")
            pill_menu = "#888890"
            bar_bg = "#b0b0b8"
            color_background = bar_bg
            bar_menu = "#909098"
        bg_dim = 0

        psx_bios = slugify(display_name) == "playstation" or dest.name.lower() == "playstation"

        theme = {
            "name": display_name,
            "author": author,
            "version": 4 if psx_bios else 2,
            "source": "psx-bios" if psx_bios else "onion-import",
            "color_background": color_background,
            "background_dim": bg_dim,
            "color_text": text or "#ffffff",
            "color_text_dim": text_dim or "#999999",
            "color_pill_selected": pill_sel or "#ffffff",
            "color_pill_unselected": pill_unsel,
            "color_pill_menu": pill_menu,
            "color_button": pill_sel or text or "#ffffff",
            "color_bar": bar or text or "#ffffff",
            "color_bar_bg": color_background,
            "color_bar_bg_menu": bar_menu,
            "background": bg_file,
            "font": font_file,
        }

        if psx_bios:
            theme.update(
                {
                    "source": "miyoo-playstation",
                    "version": 6,
                    "pill_style": "psx",
                    "grid_cell": 60,
                    "grid_origin_x": 20,
                    "grid_origin_y": 20,
                    "color_background": "#b0b0b8",
                    "background_dim": 0,
                    "color_text": "#3a4038",
                    "color_text_dim": "#4a5638",
                    "color_pill_selected": "#f0d030",
                    "color_pill_unselected": "#c8c8d0",
                    "color_pill_menu": "#888890",
                    "color_button": "#ffffff",
                    "color_bar": "#ffffff",
                    "color_bar_bg": "#b0b0b8",
                    "color_bar_bg_menu": "#909098",
                }
            )

        (dest / "theme.json").write_text(
            json.dumps(theme, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

        for old in ("preview.png",):
            p = dest / old
            if p.exists():
                p.unlink()

    print(f"Importado: {display_name} -> {dest}")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("zip", type=Path, help="Tema Onion .zip")
    p.add_argument("dest", type=Path, nargs="?", help="Carpeta salida (id del tema)")
    p.add_argument("--entry", help="Prefijo dentro del zip (p. ej. 'PICO-FANTASY by tenlevels')")
    p.add_argument("--no-bg-image", action="store_true", help="Solo color_background, sin background.png")
    p.add_argument("--no-font", action="store_true", help="No copiar fuente personalizada")
    args = p.parse_args()

    zip_path = args.zip.expanduser().resolve()
    if not zip_path.is_file():
        raise SystemExit(f"No existe: {zip_path}")

    if args.dest:
        dest = args.dest.expanduser()
        if dest.is_absolute():
            dest = dest.resolve()
        else:
            dest = (ROOT / dest.name).resolve()
    else:
        with zipfile.ZipFile(zip_path) as zf:
            prefix = find_config_prefix(zf.namelist(), args.entry)
            config = json.loads(zf.read(prefix + "config.json").decode("utf-8"))
        dest = ROOT / slugify(config.get("name") or zip_path.stem)

    import_zip(
        zip_path,
        dest,
        entry=args.entry,
        use_background_image=not args.no_bg_image,
        include_font=not args.no_font,
    )


if __name__ == "__main__":
    main()
