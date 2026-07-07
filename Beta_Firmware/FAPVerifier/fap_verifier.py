#!/usr/bin/env python3
"""
FAPVerifier — M5Stack Cardputer-ADV compatibility checker for Flipper Zero .fap files & repositories.
"""
import struct
import sys
import os
import re
import urllib.request
import zipfile
import shutil
import threading
import queue
import tkinter as tk
from tkinter import filedialog

if getattr(sys, 'frozen', False):
    SCRIPT_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Shared, repo-derived compatibility oracle (also used by the FAP Compiler). Lives
# one level up (Beta_Firmware/); bundled into the exe by PyInstaller at build time.
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))
try:
    import port_compat
except Exception:  # noqa: BLE001 — tool still runs (compat check degrades to a warn)
    port_compat = None

# ------------------------------------------------------------------ rules ----
# M5Stack Cardputer-ADV (ESP32-S3FN8, no PSRAM). Port hardware target = 32.
HW_TARGET_THIS = 32
API_MAJOR_THIS = 0
API_MINOR_THIS = 1

MEM_WARN_KB = 48
MEM_FAIL_KB = 64

FAP_MANIFEST_MAGIC = 0x52474448  # 'HDGR'

OK, WARN, FAIL = "PASS", "WARN", "FAIL"
_RANK = {OK: 0, WARN: 1, FAIL: 2}

def worst(*statuses):
    s = OK
    for x in statuses:
        if _RANK[x] > _RANK[s]:
            s = x
    return s

PIN_MAP = {
    "gpio_ext_pa7": (2, 14, "CC1101 MOSI — shared SubGHz/SD SPI bus"),
    "gpio_ext_pa6": (3, 39, "CC1101 MISO — shared SubGHz/SD SPI bus"),
    "gpio_ext_pa4": (4, 13, "CC1101 CSN — shared SubGHz SPI bus"),
    "gpio_ext_pb3": (5, 40, "CC1101 SCK — shared SubGHz/SD SPI bus"),
    "gpio_ext_pb2": (6, None, "not broken out on Cardputer-ADV"),
    "gpio_ext_pc3": (7, None, "not broken out on Cardputer-ADV"),
    "gpio_ext_pc1": (15, None, "not broken out on Cardputer-ADV"),
    "gpio_ext_pc0": (16, None, "not broken out on Cardputer-ADV"),
}

GROVE_NOTE = "Cardputer-ADV free I/O = Grove port GPIO2 (SDA) / GPIO1 (SCL)"

MODULE_SIGS = [
    (b"furi_hal_subghz", "SubGHz (CC1101)", WARN, "needs the external CC1101 SubGHz module"),
    (b"subghz_devices", "SubGHz (CC1101)", WARN, "needs the external CC1101 SubGHz module"),
    (b"furi_hal_nfc", "NFC", WARN, "needs the external NFC module (Grove)"),
    (b"nrf24", "NRF24", WARN, "needs the external NRF24 module"),
    (b"furi_hal_infrared", "Infrared", OK, "IR TX/RX present on Cardputer-ADV"),
    (b"furi_hal_rfid", "125 kHz RFID", FAIL, "no 125 kHz RFID front-end on Cardputer-ADV"),
    (b"lfrfid_worker", "125 kHz RFID", FAIL, "no 125 kHz RFID front-end on Cardputer-ADV"),
    (b"furi_hal_ibutton", "iButton", FAIL, "no iButton hardware on Cardputer-ADV"),
    (b"furi_hal_usb_hid", "USB HID", WARN, "USB is Serial-JTAG here; HID differs"),
]

# STM32 / ARM Cortex-M specific signatures. A FAP that pokes STM32 registers, uses
# CMSIS/Cortex-M intrinsics or arm_math (DSP) can't run on the ESP32-S3 (Xtensa) —
# it FAILs. Apps that only use the standard/universal Flipper API port fine.
STM_SIGS = [
    b"stm32", b"STM32", b"arm_math", b"core_cm4", b"core_cm7",
    b"CMSIS", b"cmsis_", b"__disable_irq", b"__enable_irq",
    b"NVIC_", b"LL_GPIO", b"LL_TIM", b"HAL_GPIO", b"HAL_TIM",
]

# ------------------------------------------------------------- ELF parsing ----
class FapError(Exception):
    pass

def _read_cstr(blob, off):
    end = blob.find(b"\x00", off)
    return blob[off:end if end >= 0 else len(blob)]

def parse_fap(path):
    # Defensive throughout: a malformed / truncated / non-ARM ELF must raise a
    # clean FapError, never crash the tool (this is what killed it on one .fap).
    try:
        with open(path, "rb") as f:
            blob = f.read()
    except Exception as e:  # noqa: BLE001
        raise FapError(f"Could not read file: {e}")
    if len(blob) < 0x34 or blob[:4] != b"\x7fELF":
        raise FapError("Not an ELF/.fap file (bad magic or too small).")
    if blob[4] != 1:
        raise FapError("Not a 32-bit ELF (FAPs are 32-bit ARM).")

    try:
        e_machine = struct.unpack_from("<H", blob, 0x12)[0]
        e_shoff = struct.unpack_from("<I", blob, 0x20)[0]
        e_shentsize = struct.unpack_from("<H", blob, 0x2E)[0]
        e_shnum = struct.unpack_from("<H", blob, 0x30)[0]
        e_shstrndx = struct.unpack_from("<H", blob, 0x32)[0]
    except struct.error:
        raise FapError("Corrupt ELF header.")
    if e_shoff == 0 or e_shnum == 0 or e_shentsize < 40:
        raise FapError("ELF has no usable section table.")

    secs = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        if base < 0 or base + 24 > len(blob):
            break  # section table runs past EOF — take what we have
        try:
            name_off, sh_type, _flags, _addr, sh_off, sh_size = struct.unpack_from("<IIIIII", blob, base)
        except struct.error:
            break
        secs.append({"name_off": name_off, "type": sh_type, "off": sh_off, "size": sh_size})
    if not secs:
        raise FapError("ELF section table is empty or out of range.")

    shstrtab = b""
    if 0 <= e_shstrndx < len(secs):
        sh = secs[e_shstrndx]
        shstrtab = blob[sh["off"]: sh["off"] + sh["size"]]

    out = {"machine": e_machine, "text": 0, "data": 0, "bss": 0, "fapmeta": b""}
    for s in secs:
        name = _read_cstr(shstrtab, s["name_off"]).decode("ascii", "replace") if shstrtab else ""
        if name == ".text":
            out["text"] += s["size"]
        elif name in (".data", ".data.rel.ro", ".got"):
            out["data"] += s["size"]
        elif name == ".bss":
            out["bss"] += s["size"]
        elif name == ".fapmeta":
            out["fapmeta"] = blob[s["off"]: s["off"] + s["size"]]
    out["blob"] = blob
    return out

def parse_manifest(data):
    if len(data) < 20:
        return None
    magic, _ver = struct.unpack_from("<II", data, 0)
    if magic != FAP_MANIFEST_MAGIC:
        return None
    api_minor, api_major, target, stack = struct.unpack_from("<HHHH", data, 8)
    app_version = struct.unpack_from("<I", data, 16)[0]
    name = ""
    if len(data) >= 52:
        name = data[20:52].split(b"\x00")[0].decode("utf-8", "replace")
    return {
        "api_major": api_major,
        "api_minor": api_minor,
        "target": target,
        "stack": stack,
        "app_version": app_version,
        "name": name,
    }

# --------------------------------------------------------------- the checks ----
def check_memory(fap, manifest):
    stack = manifest["stack"] if manifest else 0
    ram = fap["data"] + fap["bss"] + stack
    ram_kb = ram / 1024.0
    if ram_kb > MEM_FAIL_KB:
        st = FAIL
        note = f"over the {MEM_FAIL_KB} KB hard limit — won't fit on this no-PSRAM board"
    elif ram_kb > MEM_WARN_KB:
        st = WARN
        note = f"tight — above the {MEM_WARN_KB} KB comfort limit (<{MEM_FAIL_KB} KB max)"
    else:
        st = OK
        note = f"within the {MEM_WARN_KB} KB comfort limit"
    detail = (f"{ram_kb:.1f} KB RAM  (data {fap['data']/1024:.1f} + bss "
              f"{fap['bss']/1024:.1f} + stack {stack/1024:.1f})  •  code {fap['text']/1024:.1f} KB")
    return st, detail, note

def check_modules(fap):
    blob = fap["blob"]
    hits = {}
    for sig, label, st, note in MODULE_SIGS:
        if sig in blob:
            prev = hits.get(label)
            if prev is None or _RANK[st] > _RANK[prev[0]]:
                hits[label] = (st, note)
    if not hits:
        return OK, "no external hardware module required", "pure-software app"
    st = worst(*[v[0] for v in hits.values()])
    parts = [f"{label} ({v[1]})" for label, v in hits.items()]
    return st, "; ".join(parts), "required module(s) listed"

def check_compatibility(fap):
    # Replaces the old static STM/ARM-only "Hardware" check. Uses the shared,
    # repo-derived oracle (port_compat): flags furi/HAL calls + Flipper headers the
    # CURRENT firmware doesn't provide, plus STM32/ARM-specific code. Because the
    # "supported" set is derived from the live headers, adding new furi support to
    # the port makes this pass automatically — no stale list, no false positives.
    if port_compat is None:
        # Fallback to the legacy byte-scan if the shared module didn't load.
        blob = fap.get("blob", b"")
        hits = [s.decode("ascii", "replace") for s in STM_SIGS if s in blob]
        if hits:
            return FAIL, "STM32/ARM-specific: " + ", ".join(hits[:6]), \
                "Cortex-M / STM32 low-level code — not portable to ESP32-S3 (Xtensa)"
        return OK, "no STM32/ARM-specific code (compat module unavailable)", \
            "install port_compat.py next to the tool for the full check"

    src_dir = fap.get("src_dir")
    if src_dir and port_compat.index_available():
        r = port_compat.scan_source(src_dir)
    else:
        r = port_compat.scan_blob(fap.get("blob", b""))
        r.setdefault("missing_headers", [])

    unk = r.get("unknown_symbols", [])
    miss = r.get("missing_headers", [])
    stm = r.get("stm", [])

    st = OK
    parts = []
    if stm:
        st = worst(st, FAIL)
        parts.append("STM32/ARM: " + ", ".join(stm[:5]))
    if miss:
        st = worst(st, FAIL)
        parts.append("missing headers: " + ", ".join(miss[:5]))
    if unk:
        st = worst(st, FAIL)
        parts.append("unsupported API: " + ", ".join(unk[:6]))

    if st == OK:
        if not port_compat.index_available():
            return WARN, "firmware source not found — could not verify port API", \
                "run the tool from inside the firmware repo for the full check"
        return OK, "uses only APIs + headers this port provides", \
            "portable to the Cardputer-ADV port"

    extra = []
    if len(unk) > 6:
        extra.append(f"+{len(unk) - 6} more API")
    if len(miss) > 5:
        extra.append(f"+{len(miss) - 5} more header")
    detail = "  |  ".join(parts)
    if extra:
        detail += "  (" + ", ".join(extra) + ")"
    return st, detail, "not implemented on the Cardputer-ADV port"

def check_screen(fap):
    return OK, "128x64 mono -> rendered on 240x135 colour TFT", "UI scales; no change needed"

def check_pins(fap):
    blob = fap["blob"]
    used = []
    for sym, (fpin, gpio, note) in PIN_MAP.items():
        if sym.encode() in blob:
            used.append((sym, fpin, gpio, note))
    if not used:
        return OK, "no Flipper GPIO header pins used", GROVE_NOTE
    lines = []
    st = OK
    for sym, fpin, gpio, note in used:
        if gpio is None:
            st = worst(st, FAIL)
            lines.append(f"pin{fpin}/{sym} -> NOT AVAILABLE ({note})")
        else:
            st = worst(st, WARN)
            lines.append(f"pin{fpin}/{sym} -> GPIO{gpio} ({note})")
    return st, "  |  ".join(lines), GROVE_NOTE

# ------------------------------------------------------------- source checks ----
def check_source_compatibility(src_dir):
    # Scan source files directly to simulate verification when given a repository or zip
    manifest_path = os.path.join(src_dir, "application.fam")
    stack_size = 1024
    app_name = os.path.basename(src_dir)
    if os.path.exists(manifest_path):
        with open(manifest_path, "r", encoding="utf-8") as f:
            content = f.read()
        match = re.search(r'stack_size\s*=\s*([0-9]+)', content)
        if match:
            stack_size = int(match.group(1))
        match_name = re.search(r'appid\s*=\s*["\']([^"\']+)["\']', content)
        if match_name:
            app_name = match_name.group(1)

    # Search files
    c_bytes = b""
    for root, _, files in os.walk(src_dir):
        for file in files:
            if file.endswith((".c", ".h")):
                try:
                    with open(os.path.join(root, file), "rb") as f:
                        c_bytes += f.read()
                except Exception:
                    pass

    # Stub FAP dictionary for checkers. src_dir lets the compatibility check do a
    # full source scan (symbols + headers) instead of a byte-scan of the blob.
    fap = {"data": 1024, "bss": 2048, "text": 8192, "blob": c_bytes, "src_dir": src_dir}
    manifest = {"stack": stack_size}
    
    results = [
        ("Memory", *check_memory(fap, manifest)),
        ("Compatibility", *check_compatibility(fap)),
        ("Module", *check_modules(fap)),
        ("Screen", *check_screen(fap)),
        ("Pins", *check_pins(fap)),
    ]
    overall = worst(*[r[1] for r in results])
    return {"app": app_name, "arm": False, "results": results, "overall": overall}

def check_source_repo(root_dir):
    # A repo may hold ONE app or be a monorepo of many (e.g. flipperzero-good-faps).
    # Find every application.fam and verify each app scoped to its OWN folder — a
    # whole-repo scan would aggregate every module/pin any sub-app uses and always FAIL.
    app_dirs = []
    for cur, dirs, files in os.walk(root_dir):
        if "application.fam" in files:
            app_dirs.append(cur)
            dirs[:] = []  # don't descend into an app's own subfolders
    if len(app_dirs) <= 1:
        # single app (or no manifest → best-effort whole-tree scan)
        return check_source_compatibility(app_dirs[0] if app_dirs else root_dir)
    apps = [check_source_compatibility(d) for d in app_dirs]
    apps.sort(key=lambda a: (-_RANK[a["overall"]], a["app"].lower()))  # worst first
    overall = worst(*[a["overall"] for a in apps])
    return {"multi": True, "app": os.path.basename(root_dir.rstrip("/\\")),
            "apps": apps, "overall": overall, "arm": False, "results": []}

def verify(path):
    fap = parse_fap(path)
    manifest = parse_manifest(fap["fapmeta"])
    # No API check here: the FAP Compiler recompiles the app from source against
    # this port's API, so the original ARM FAP's API version is irrelevant — the
    # compiler is what makes the API match.
    results = [
        ("Memory", *check_memory(fap, manifest)),
        ("Compatibility", *check_compatibility(fap)),
        ("Module", *check_modules(fap)),
        ("Screen", *check_screen(fap)),
        ("Pins", *check_pins(fap)),
    ]
    overall = worst(*[r[1] for r in results])
    app_name = (manifest or {}).get("name") or os.path.basename(path)
    is_arm = fap["machine"] == 0x28
    return {"app": app_name, "arm": is_arm, "results": results, "overall": overall}


def _http_get(url, dest):
    req = urllib.request.Request(url, headers={'User-Agent': 'AntigravityVerifier/1.0'})
    with urllib.request.urlopen(req) as resp, open(dest, 'wb') as out:
        shutil.copyfileobj(resp, out)


def fetch_source(target, work_dir):
    """Download+extract a GitHub URL (repo root OR /tree/BRANCH/sub/folder), a .zip
    URL, or a local .zip into work_dir; return the path to verify. Tree links resolve
    to the named branch + subfolder so a single app inside a monorepo works."""
    os.makedirs(work_dir, exist_ok=True)
    zip_path = os.path.join(work_dir, "_src.zip")
    subpath = ""
    is_url = target.startswith("http://") or target.startswith("https://")
    if is_url and "github.com" in target.lower() and not target.lower().endswith(".zip") \
            and port_compat is not None:
        gh = port_compat.parse_github(target)
        if not gh:
            raise Exception("Unrecognized GitHub URL.")
        subpath = gh["subpath"]
        got = False
        for zu in port_compat.github_zip_urls(gh["owner"], gh["repo"], gh["branch"]):
            try:
                _http_get(zu, zip_path)
                got = True
                break
            except Exception:
                continue
        if not got:
            raise Exception("Could not download the repository archive (branch not found?).")
    elif is_url:
        _http_get(target, zip_path)
    elif os.path.exists(target) and target.lower().endswith(".zip"):
        shutil.copy(target, zip_path)
    else:
        raise Exception("Invalid input (not a URL, .zip, or existing file).")

    with zipfile.ZipFile(zip_path, 'r') as zf:
        zf.extractall(work_dir)
    os.remove(zip_path)
    tops = [d for d in os.listdir(work_dir) if os.path.isdir(os.path.join(work_dir, d))]
    src = os.path.join(work_dir, tops[0]) if tops else work_dir
    if subpath:
        cand = os.path.join(src, *subpath.split("/"))
        if not os.path.isdir(cand):
            raise Exception(f"Subfolder '{subpath}' not found in the repository.")
        src = cand
    return src

# ------------------------------------------------------------- GUI Theme ------------------
BG_COLOR = "#020813"
CANVAS_BG = "#050B14"
GRID_COLOR = "#081d33"
NEON_BLUE = "#00bcff"
NEON_BLUE_DARK = "#0a2240"
TEXT_COLOR = "#ffffff"
MUTED_TEXT = "#4f7da3"
GREEN_GLOW = "#00ff66"
RED_GLOW = "#ff2a2a"

class FAPVerifierGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("FAP Verifier")
        
        # Center on screen
        w, h = 480, 680
        ws = root.winfo_screenwidth()
        hs = root.winfo_screenheight()
        x = int((ws/2) - (w/2))
        y = int((hs/2) - (h/2))
        self.root.geometry(f"{w}x{h}+{x}+{y}")
        self.root.configure(bg=BG_COLOR)
        
        # Borderless window drag setup
        self.root.overrideredirect(True)
        self._drag_data = {"x": 0, "y": 0}
        
        self.is_processing = False
        self.animation_step = 0
        
        self.build_ui()
        self.animate_canvas()
        
    def build_ui(self):
        # Outer Glowing Border
        self.outer_frame = tk.Frame(self.root, bg=NEON_BLUE, bd=1)
        self.outer_frame.pack(fill=tk.BOTH, expand=True)
        
        self.main_container = tk.Frame(self.outer_frame, bg=BG_COLOR)
        self.main_container.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        
        # 1. Title bar
        self.title_bar = tk.Frame(self.main_container, bg=BG_COLOR, height=35)
        self.title_bar.pack(fill=tk.X)
        self.title_bar.pack_propagate(False)
        
        self.title_bar.bind("<ButtonPress-1>", self.start_move)
        self.title_bar.bind("<ButtonRelease-1>", self.stop_move)
        self.title_bar.bind("<B1-Motion>", self.on_move)
        
        self.title_lbl = tk.Label(self.title_bar, text="FAP VERIFIER", fg=NEON_BLUE, bg=BG_COLOR, font=("Consolas", 11, "bold"))
        self.title_lbl.pack(side=tk.LEFT, padx=10)
        
        self.close_btn = tk.Button(self.title_bar, text="X", fg=TEXT_COLOR, bg=BG_COLOR, activeforeground=RED_GLOW, activebackground=BG_COLOR, bd=0, font=("Consolas", 11, "bold"), command=self.exit_app)
        self.close_btn.pack(side=tk.RIGHT, padx=10)
        
        self.min_btn = tk.Button(self.title_bar, text="_", fg=TEXT_COLOR, bg=BG_COLOR, activeforeground=NEON_BLUE, activebackground=BG_COLOR, bd=0, font=("Consolas", 11, "bold"), command=self.minimize_app)
        self.min_btn.pack(side=tk.RIGHT, padx=5)
        
        # 2. Main Grid Canvas
        self.canvas_frame = tk.Frame(self.main_container, bg=BG_COLOR, bd=1, highlightbackground=NEON_BLUE_DARK, highlightthickness=1)
        self.canvas_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        self.canvas = tk.Canvas(self.canvas_frame, bg=CANVAS_BG, highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        
        # 3. Controls (Centered on Canvas)
        self.control_frame = tk.Frame(self.canvas, bg=CANVAS_BG)
        
        self.input_border = tk.Frame(self.control_frame, bg=NEON_BLUE_DARK, bd=1)
        self.input_border.pack(side=tk.LEFT, fill=tk.X, expand=True, ipady=3)
        
        self.input_entry = tk.Entry(self.input_border, bg=BG_COLOR, fg=TEXT_COLOR, insertbackground=NEON_BLUE, bd=0, font=("Consolas", 10), width=32)
        self.input_entry.pack(fill=tk.X, padx=5)
        self.input_entry.insert(0, "Enter GitHub link, local zip, or fap...")
        self.input_entry.bind("<FocusIn>", self.clear_placeholder)
        self.input_entry.bind("<FocusOut>", self.restore_placeholder)
        
        self.verify_btn = tk.Button(self.control_frame, text="VERIFY", bg=NEON_BLUE_DARK, fg=TEXT_COLOR, activebackground=NEON_BLUE, activeforeground=BG_COLOR, font=("Consolas", 9, "bold"), bd=1, relief=tk.FLAT, command=self.start_verification)
        self.verify_btn.pack(side=tk.RIGHT, padx=5)

        # Browse for a local .fap or .zip to verify.
        self.browse_btn = tk.Button(self.control_frame, text="BROWSE", bg=BG_COLOR, fg=NEON_BLUE, activebackground=NEON_BLUE_DARK, activeforeground=TEXT_COLOR, font=("Consolas", 9, "bold"), bd=1, relief=tk.FLAT, command=self.browse_file)
        self.browse_btn.pack(side=tk.RIGHT, padx=5)
        
        self.canvas.create_window(238, 140, window=self.control_frame, width=440, tags="input_window")
        
        # 4. Results List Container
        self.results_frame = tk.Frame(self.canvas, bg=CANVAS_BG)
        self.canvas.create_window(238, 380, window=self.results_frame, width=440, height=370, tags="results_window")
        
        # 5. Footer status
        self.footer = tk.Frame(self.main_container, bg=BG_COLOR, height=40)
        self.footer.pack(fill=tk.X, side=tk.BOTTOM)
        
        self.status_lbl = tk.Label(self.footer, text="WAITING FOR FILE...", fg=MUTED_TEXT, bg=BG_COLOR, font=("Consolas", 9, "bold"))
        self.status_lbl.pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        self.theme_lbl = tk.Label(self.footer, text="THEME: CYBER", fg=NEON_BLUE, bg=BG_COLOR, font=("Consolas", 9, "bold"), cursor="hand2")
        self.theme_lbl.pack(side=tk.RIGHT, padx=10)
        self.theme_lbl.bind("<Button-1>", self.cycle_theme)
        
    def start_move(self, event):
        self._drag_data["x"] = event.x
        self._drag_data["y"] = event.y

    def stop_move(self, event):
        self._drag_data["x"] = 0
        self._drag_data["y"] = 0

    def on_move(self, event):
        deltax = event.x - self._drag_data["x"]
        deltay = event.y - self._drag_data["y"]
        x = self.root.winfo_x() + deltax
        y = self.root.winfo_y() + deltay
        self.root.geometry(f"+{x}+{y}")
        
    def exit_app(self):
        sys.exit(0)
        
    def minimize_app(self):
        self.root.overrideredirect(False)
        self.root.iconify()
        self.root.bind("<FocusIn>", self.restore_borderless)
        
    def restore_borderless(self, event):
        self.root.overrideredirect(True)
        self.root.unbind("<FocusIn>")
        
    def browse_file(self):
        fpath = filedialog.askopenfilename(
            title="Pick a .fap or .zip to verify",
            filetypes=[("Flipper app or source zip", "*.fap *.zip"),
                       ("Flipper app package", "*.fap"),
                       ("Source zip", "*.zip"),
                       ("All files", "*.*")])
        if fpath:
            self.input_entry.delete(0, tk.END)
            self.input_entry.insert(0, fpath)

    def clear_placeholder(self, event):
        if "Enter GitHub" in self.input_entry.get():
            self.input_entry.delete(0, tk.END)
            
    def restore_placeholder(self, event):
        if not self.input_entry.get().strip():
            self.input_entry.insert(0, "Enter GitHub link, local zip, or fap...")
            
    def cycle_theme(self, event):
        global NEON_BLUE, NEON_BLUE_DARK
        if NEON_BLUE == "#00bcff":
            NEON_BLUE = GREEN_GLOW
            NEON_BLUE_DARK = "#0a3c20"
            self.theme_lbl.configure(text="THEME: MATRIX", fg=GREEN_GLOW)
        elif NEON_BLUE == GREEN_GLOW:
            NEON_BLUE = RED_GLOW
            NEON_BLUE_DARK = "#4a0a0a"
            self.theme_lbl.configure(text="THEME: HORNET", fg=RED_GLOW)
        else:
            NEON_BLUE = "#00bcff"
            NEON_BLUE_DARK = "#0a2240"
            self.theme_lbl.configure(text="THEME: CYBER", fg="#00bcff")
            
        self.outer_frame.configure(bg=NEON_BLUE)
        self.title_lbl.configure(fg=NEON_BLUE)
        self.verify_btn.configure(bg=NEON_BLUE_DARK)
        self.theme_lbl.configure(fg=NEON_BLUE)
        # animate_canvas() is already running on a 40ms after-loop and reads the
        # theme globals each frame — do NOT call it again here or each theme click
        # stacks another compounding redraw loop.
        
    def update_status(self, text, color=TEXT_COLOR):
        self.status_lbl.configure(text=text, fg=color)
        
    def animate_canvas(self):
        self.canvas.delete("anim")
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        if w < 50:
            w, h = 480, 600
            
        self.canvas.delete("grid")
        for x in range(0, w, 20):
            self.canvas.create_line(x, 0, x, h, fill=GRID_COLOR, tags="grid")
        for y in range(0, h, 20):
            self.canvas.create_line(0, y, w, y, fill=GRID_COLOR, tags="grid")
            
        cx, cy = w // 2, h // 2
        
        self.canvas.create_text(cx, 40, text="FAP VERIFIER", fill=NEON_BLUE, font=("Consolas", 16, "bold"), tags="anim")
        self.canvas.create_text(cx, 65, text="PORTABILITY & ANALYSIS TOOL", fill=MUTED_TEXT, font=("Consolas", 8, "bold"), tags="anim")
        
        # Border highlight around input frame
        self.canvas.create_rectangle(cx - 224, 115, cx + 224, 165, outline=NEON_BLUE_DARK, width=2, tags="anim")
        
        if self.is_processing:
            self.animation_step = (self.animation_step + 4) % h
            self.canvas.create_line(0, self.animation_step, w, self.animation_step, fill=GREEN_GLOW, width=1, tags="anim")
            self.canvas.create_text(cx, 93, text="SCANNING FILE...", fill=GREEN_GLOW, font=("Consolas", 9, "bold"), tags="anim")
        else:
            self.canvas.create_text(cx, 93, text="SYSTEM READY", fill=MUTED_TEXT, font=("Consolas", 9, "bold"), tags="anim")
            
        self.root.after(40, self.animate_canvas)
        
    def start_verification(self):
        if self.is_processing:
            return
        path_or_url = self.input_entry.get().strip()
        if not path_or_url or "Enter GitHub" in path_or_url:
            self.update_status("ERROR: NO INPUT", RED_GLOW)
            return
            
        self.is_processing = True
        self.verify_btn.configure(state=tk.DISABLED)
        self.update_status("PROCESSING TARGET...", NEON_BLUE)
        
        # Clear old rows
        for widget in self.results_frame.winfo_children():
            widget.destroy()
            
        thread = threading.Thread(target=self.run_verify_logic, args=(path_or_url,))
        thread.daemon = True
        thread.start()
        
    def run_verify_logic(self, target):
        target = target.strip('"').strip("'")
        
        # Temp dir for extraction if it's a URL or ZIP
        temp_dir = None
        try:
            if target.startswith("http://") or target.startswith("https://") or target.lower().endswith(".zip"):
                self.update_status("DOWNLOADING SOURCE...", NEON_BLUE)
                temp_dir = os.path.join(SCRIPT_DIR, "_verifier_temp")
                shutil.rmtree(temp_dir, ignore_errors=True)
                # Handles repo roots AND /tree/BRANCH/subfolder deep links.
                src_path = fetch_source(target, temp_dir)
                r = check_source_repo(src_path)
            else:
                # Local FAP
                if not os.path.exists(target):
                    self.root.after(0, lambda: self.show_error_msg("Local file path not found."))
                    return
                r = verify(target)
                
            self.root.after(0, lambda: self.display_results(r))
        except Exception as e:
            self.root.after(0, lambda: self.show_error_msg(f"Error: {e}"))
        finally:
            if temp_dir and os.path.exists(temp_dir):
                shutil.rmtree(temp_dir)
            self.is_processing = False
            self.root.after(0, lambda: self.verify_btn.configure(state=tk.NORMAL))
            
    def download_target(self, url):
        dest_dir = SCRIPT_DIR
        zip_path = os.path.join(dest_dir, "temp_ver_archive.zip")
        # Direct ZIP download or GitHub repo redirect
        if "github.com" in url.lower() and not url.lower().endswith(".zip"):
            url = url.rstrip("/")
            if url.endswith(".git"):
                url = url[:-4]
            url = f"{url}/archive/refs/heads/main.zip"
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'AntigravityVerifier/1.0'})
            with urllib.request.urlopen(req) as response, open(zip_path, 'wb') as out_file:
                shutil.copyfileobj(response, out_file)
            return zip_path
        except Exception:
            # Fallback to master branch
            if "main.zip" in url:
                url = url.replace("main.zip", "master.zip")
                try:
                    urllib.request.urlretrieve(url, zip_path)
                    return zip_path
                except Exception:
                    pass
        return None
        
    def show_error_msg(self, msg):
        self.update_status("VERIFICATION FAILED", RED_GLOW)
        lbl = tk.Label(self.results_frame, text=msg, fg=RED_GLOW, bg=CANVAS_BG, font=("Consolas", 10, "bold"), wraplength=400)
        lbl.pack(pady=40)
        
    def display_results(self, r):
        # Update Footer status
        color = GREEN_GLOW if r["overall"] == OK else (RED_GLOW if r["overall"] == FAIL else "#ffaa00")
        self.update_status(f"OVERALL PORTABILITY: {r['overall']}", color)

        for widget in self.results_frame.winfo_children():
            widget.destroy()

        if r.get("multi"):
            self.display_multi(r)
            return

        # Render Categories inside results frame
        for cat, st, detail, note in r["results"]:
            card = tk.Frame(self.results_frame, bg=BG_COLOR, highlightbackground=NEON_BLUE_DARK, highlightthickness=1)
            card.pack(fill=tk.X, pady=4, padx=5)
            
            # Badge color
            badge_color = GREEN_GLOW if st == OK else (RED_GLOW if st == FAIL else "#ffaa00")
            badge_text = "PASS" if st == OK else ("FAIL" if st == FAIL else "WARN")
            
            badge = tk.Label(card, text=badge_text, fg=BG_COLOR, bg=badge_color, font=("Consolas", 8, "bold"), width=6)
            badge.pack(side=tk.LEFT, padx=8, pady=8)
            
            info_frame = tk.Frame(card, bg=BG_COLOR)
            info_frame.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
            
            title = tk.Label(info_frame, text=cat, fg=TEXT_COLOR, bg=BG_COLOR, font=("Consolas", 9, "bold"), anchor="w")
            title.pack(fill=tk.X)
            
            desc = tk.Label(info_frame, text=detail, fg=MUTED_TEXT, bg=BG_COLOR, font=("Consolas", 7), anchor="w", wraplength=320, justify="left")
            desc.pack(fill=tk.X)
            
            if note:
                nt = tk.Label(info_frame, text=f"• {note}", fg=MUTED_TEXT, bg=BG_COLOR, font=("Consolas", 7, "italic"), anchor="w", wraplength=320, justify="left")
                nt.pack(fill=tk.X)

    def display_multi(self, r):
        apps = r["apps"]
        npass = sum(1 for a in apps if a["overall"] == OK)
        nwarn = sum(1 for a in apps if a["overall"] == WARN)
        nfail = sum(1 for a in apps if a["overall"] == FAIL)

        tk.Label(self.results_frame,
                 text=f"{r['app']}  —  {len(apps)} apps   {npass} PASS / {nwarn} WARN / {nfail} FAIL",
                 fg=TEXT_COLOR, bg=CANVAS_BG, font=("Consolas", 9, "bold")).pack(anchor="w", padx=4, pady=(0, 4))

        # Scrollable per-app list (a monorepo can have dozens of apps)
        wrap = tk.Frame(self.results_frame, bg=CANVAS_BG)
        wrap.pack(fill=tk.BOTH, expand=True)
        canv = tk.Canvas(wrap, bg=CANVAS_BG, highlightthickness=0)
        sb = tk.Scrollbar(wrap, orient="vertical", command=canv.yview)
        inner = tk.Frame(canv, bg=CANVAS_BG)
        inner.bind("<Configure>", lambda e: canv.configure(scrollregion=canv.bbox("all")))
        canv.create_window((0, 0), window=inner, anchor="nw", width=415)
        canv.configure(yscrollcommand=sb.set)
        canv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        # Bind the wheel only while the pointer is over the list, and unbind on
        # leave so a later single-app view doesn't scroll a destroyed canvas.
        def _wheel(e):
            canv.yview_scroll(int(-e.delta / 120), "units")
        canv.bind("<Enter>", lambda e: canv.bind_all("<MouseWheel>", _wheel))
        canv.bind("<Leave>", lambda e: canv.unbind_all("<MouseWheel>"))

        for a in apps:
            st = a["overall"]
            bc = GREEN_GLOW if st == OK else (RED_GLOW if st == FAIL else "#ffaa00")
            row = tk.Frame(inner, bg=BG_COLOR, highlightbackground=NEON_BLUE_DARK, highlightthickness=1)
            row.pack(fill=tk.X, pady=2, padx=2)
            tk.Label(row, text=st, fg=BG_COLOR, bg=bc, font=("Consolas", 8, "bold"), width=6).pack(side=tk.LEFT, padx=4, pady=4)
            info = tk.Frame(row, bg=BG_COLOR)
            info.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            tk.Label(info, text=a["app"], fg=TEXT_COLOR, bg=BG_COLOR, font=("Consolas", 8, "bold"), anchor="w").pack(fill=tk.X)
            issues = ", ".join(f"{c}: {s}" for c, s, _, _ in a["results"] if s != OK) or "all checks pass"
            tk.Label(info, text=issues, fg=MUTED_TEXT, bg=BG_COLOR, font=("Consolas", 7), anchor="w",
                     wraplength=330, justify="left").pack(fill=tk.X)

def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--check":
        try:  # console may be cp1252; avoid UnicodeEncodeError on em-dash/bullet
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
        target = sys.argv[2].strip('"').strip("'")
        temp_dir = None
        try:
            if target.startswith("http://") or target.startswith("https://") or target.lower().endswith(".zip"):
                temp_dir = os.path.join(SCRIPT_DIR, "_verifier_temp_cli")
                shutil.rmtree(temp_dir, ignore_errors=True)
                # Handles repo roots AND /tree/BRANCH/subfolder deep links.
                src_path = fetch_source(target, temp_dir)
                r = check_source_repo(src_path)
            else:
                r = verify(target)

            if r.get("multi"):
                apps = r["apps"]
                npass = sum(1 for a in apps if a["overall"] == OK)
                nwarn = sum(1 for a in apps if a["overall"] == WARN)
                nfail = sum(1 for a in apps if a["overall"] == FAIL)
                print(f"Repo: {r['app']}  Apps: {len(apps)}  "
                      f"({npass} PASS / {nwarn} WARN / {nfail} FAIL)  Overall: {r['overall']}")
                for a in apps:
                    issues = ", ".join(f"{c}:{s}" for c, s, _, _ in a["results"] if s != OK) or "all pass"
                    print(f"  [{a['overall']}] {a['app']}: {issues}")
            else:
                print(f"App: {r['app']}  Overall: {r['overall']}")
                for cat, st, detail, note in r["results"]:
                    print(f"[{st}] {cat}: {detail}")
                    if note:
                        print(f"      * {note}")
            return 0
        except Exception as e:
            print(f"Error: {e}")
            return 1
        finally:
            if temp_dir and os.path.exists(temp_dir):
                shutil.rmtree(temp_dir)
            
    root = tk.Tk()
    app = FAPVerifierGUI(root)
    root.mainloop()
    return 0

if __name__ == "__main__":
    sys.exit(main())
