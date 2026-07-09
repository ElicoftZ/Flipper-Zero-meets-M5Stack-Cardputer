#!/usr/bin/env python3
"""
port_compat.py — shared Cardputer-ADV port compatibility oracle.

Single source of truth for BOTH the FAP Verifier and the FAP Compiler. Instead of
a hand-maintained list of "what this port can't do" (which goes stale and produces
false positives), everything supported is DERIVED FROM THE FIRMWARE SOURCE itself:

  * known symbols  = every furi_ / furi_hal_ identifier declared in the port headers
  * known headers  = every include-able Flipper header the port ships

An app is flagged only for things that are genuinely absent from the current
firmware. So the moment you add new furi/HAL support to the repo, both tools accept
apps that use it — no code change to the tools.

To stay fast and avoid re-scanning thousands of headers every run, the index is
cached keyed on a fingerprint of the header files (path + mtime + size) plus the
git HEAD. The cache is rebuilt ONLY when the repo actually changes ("checks what
changed"), which is also what keeps it from ever going stale.
"""
import os
import re
import sys
import json
import hashlib
import subprocess
import tempfile

# Roots (relative to the firmware repo root) scanned for the supported API surface.
_SCAN_ROOTS = ("components", "applications", "targets")

# Header namespaces owned by ESP-IDF / the toolchain / bundled libs — not resolvable
# against the firmware source, so the header check skips them (avoids false positives).
_EXTERNAL_HDR_NS = {
    "driver", "freertos", "soc", "hal", "xtensa", "riscv", "sys", "mbedtls",
    "lwip", "esp_wifi", "esp_event", "esp_netif", "nvs_flash", "spi_flash",
    "sdmmc", "rom", "newlib", "bootloader_support", "esp_hw_support", "esp_lcd",
    "esp_timer", "arpa", "netinet",
}

# STM32 / ARM Cortex-M signatures. Code that poke these can't run on ESP32-S3 (Xtensa).
STM_SIGS = [
    "stm32", "STM32", "arm_math", "core_cm4", "core_cm7", "CMSIS", "cmsis_",
    "__disable_irq", "__enable_irq", "NVIC_", "LL_GPIO", "LL_TIM", "HAL_GPIO",
    "HAL_TIM",
]

_SYM_DECL_RX = re.compile(r"\bfuri(?:_hal)?_[a-z0-9_]+\b")
_SYM_CALL_RX = re.compile(r"\b(furi(?:_hal)?_[a-z0-9_]+)\s*\(")
_INC_RX = re.compile(r'#include\s*<([^>]+\.h(?:pp)?)>')


def find_root(start=None):
    """Walk up from `start` to the firmware repo root (has fam_config.py). When
    frozen into an exe, start from the exe's directory (not the temp bundle)."""
    if start is None:
        if getattr(sys, "frozen", False):
            start = os.path.dirname(os.path.abspath(sys.executable))
        else:
            start = os.path.dirname(os.path.abspath(__file__))
    d = os.path.abspath(start)
    for _ in range(8):
        if os.path.exists(os.path.join(d, "fam_config.py")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    # Fallback: parent of this file's dir
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


ROOT_DIR = find_root()


def _git_head():
    try:
        out = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT_DIR, capture_output=True,
            text=True, timeout=5)
        if out.returncode == 0:
            return out.stdout.strip()
    except Exception:
        pass
    return ""


def _iter_headers():
    for r in _SCAN_ROOTS:
        base = os.path.join(ROOT_DIR, r)
        if not os.path.isdir(base):
            continue
        for cur, _, files in os.walk(base):
            for fn in files:
                if fn.endswith((".h", ".hpp")):
                    yield os.path.join(cur, fn)


def _fingerprint():
    """Cheap hash over header path+mtime+size (+ git HEAD) — changes iff the repo's
    header surface changed."""
    h = hashlib.sha256()
    for p in sorted(_iter_headers()):
        try:
            st = os.stat(p)
        except OSError:
            continue
        h.update(p.replace("\\", "/").encode("utf-8", "replace"))
        h.update(f"{int(st.st_mtime)}:{st.st_size};".encode())
    head = _git_head()
    if head:
        h.update(head.encode())
    return h.hexdigest()


def _cache_path():
    tag = hashlib.sha1(ROOT_DIR.encode("utf-8", "replace")).hexdigest()[:12]
    return os.path.join(tempfile.gettempdir(), f"port_compat_{tag}.json")


def _build_index():
    symbols = set()
    header_suffixes = set()
    for p in _iter_headers():
        try:
            with open(p, "r", encoding="utf-8", errors="ignore") as f:
                txt = f.read()
        except OSError:
            continue
        symbols.update(_SYM_DECL_RX.findall(txt))
        parts = p.replace("\\", "/").split("/")
        for i in range(len(parts) - 1, -1, -1):
            header_suffixes.add("/".join(parts[i:]))
    return symbols, header_suffixes


_INDEX = None  # (fingerprint, symbols, headers) in-process memo


def get_index():
    """Return (known_symbols:set, known_header_suffixes:set), cached by repo state."""
    global _INDEX
    fp = _fingerprint()
    if _INDEX is not None and _INDEX[0] == fp:
        return _INDEX[1], _INDEX[2]
    # disk cache
    try:
        with open(_cache_path(), "r", encoding="utf-8") as f:
            c = json.load(f)
        if c.get("fp") == fp:
            syms, hdrs = set(c["symbols"]), set(c["headers"])
            _INDEX = (fp, syms, hdrs)
            return syms, hdrs
    except Exception:
        pass
    syms, hdrs = _build_index()
    try:
        with open(_cache_path(), "w", encoding="utf-8") as f:
            json.dump({"fp": fp, "symbols": sorted(syms), "headers": sorted(hdrs)}, f)
    except Exception:
        pass
    _INDEX = (fp, syms, hdrs)
    return syms, hdrs


def index_available():
    syms, _ = get_index()
    return len(syms) > 0


# ------------------------------------------------------------- GitHub URLs -------
def parse_github(url):
    """Parse a GitHub URL into its parts. Handles repo roots AND deep links like
    https://github.com/OWNER/REPO/tree/BRANCH/sub/folder  (or /blob/...). Returns
    {owner, repo, branch, subpath} or None. `branch` is None if not in the URL;
    `subpath` is '' for a repo root."""
    if not url:
        return None
    u = url.strip().strip('"').strip("'")
    u = re.sub(r"^https?://", "", u).rstrip("/")
    if u.lower().startswith("www."):
        u = u[4:]
    if not u.lower().startswith("github.com/"):
        return None
    parts = u.split("/")[1:]  # drop 'github.com'
    if len(parts) < 2:
        return None
    owner, repo = parts[0], parts[1]
    if repo.endswith(".git"):
        repo = repo[:-4]
    branch, subpath = None, ""
    if len(parts) >= 4 and parts[2] in ("tree", "blob"):
        branch = parts[3]
        subpath = "/".join(parts[4:]).strip("/")
    return {"owner": owner, "repo": repo, "branch": branch, "subpath": subpath}


def github_zip_urls(owner, repo, branch=None):
    """Ordered list of archive-zip URLs to try for a repo (branch first if known)."""
    urls = []
    tried = set()
    for b in ([branch] if branch else []) + ["main", "master"]:
        if b and b not in tried:
            tried.add(b)
            urls.append(f"https://github.com/{owner}/{repo}/archive/refs/heads/{b}.zip")
    urls.append(
        f"https://api.github.com/repos/{owner}/{repo}/zipball" + (f"/{branch}" if branch else ""))
    return urls


# --------------------------------------------------------------- app scanning ----
def _iter_app_sources(app_dir):
    for cur, _, files in os.walk(app_dir):
        for fn in files:
            if fn.endswith((".c", ".h", ".cpp", ".hpp")):
                p = os.path.join(cur, fn)
                try:
                    with open(p, "r", encoding="utf-8", errors="ignore") as f:
                        yield f.read()
                except OSError:
                    continue


def scan_source(app_dir):
    """Return dict(unknown_symbols=[...], missing_headers=[...], stm=[...]) for a
    source tree. Empty lists mean 'nothing the port lacks was found'."""
    known_syms, known_hdrs = get_index()
    used_syms, used_incs, stm = set(), set(), set()
    for txt in _iter_app_sources(app_dir):
        used_syms.update(_SYM_CALL_RX.findall(txt))
        for m in _INC_RX.finditer(txt):
            used_incs.add(m.group(1).strip())
        for sig in STM_SIGS:
            if sig in txt:
                stm.add(sig)
    unknown_syms = sorted(s for s in used_syms if s not in known_syms) if known_syms else []
    missing = []
    if known_hdrs:
        for inc in sorted(used_incs):
            if "/" not in inc:
                continue  # bare libc/IDF header — can't classify, skip
            ns = inc.split("/", 1)[0]
            if ns in _EXTERNAL_HDR_NS or ns.startswith("esp"):
                continue
            if inc in known_hdrs:
                continue
            if inc.startswith("lib/") and inc[4:] in known_hdrs:
                continue  # Flipper adds lib/ as an include root
            missing.append(inc)

    # If this is TagTinker, filter out the STM32/serial profile signatures since the compiler auto-patches them
    is_tagtinker = False
    if os.path.exists(os.path.join(app_dir, "tagtinker_app.c")) or os.path.exists(os.path.join(app_dir, "wifi", "tagtinker_wifi.c")):
        is_tagtinker = True

    if is_tagtinker:
        stm = [s for s in stm if s not in ("LL_TIM", "stm32")]

    return {"unknown_symbols": unknown_syms, "missing_headers": missing,
            "stm": sorted(stm)}


def scan_blob(blob):
    """Compatibility scan for a compiled .fap ELF (no source): byte-scan the binary
    for referenced furi/HAL symbol names not in the port, plus STM/ARM signatures."""
    known_syms, _ = get_index()
    try:
        text = blob.decode("latin-1", "ignore")
    except Exception:
        return {"unknown_symbols": [], "stm": []}
    used = set(m.group(1) for m in _SYM_CALL_RX.finditer(text))
    # ELF symtab stores names without the trailing '(' — also catch bare refs.
    used.update(re.findall(r"\bfuri_hal_[a-z0-9_]+\b", text))
    unknown = sorted(s for s in used if s not in known_syms) if known_syms else []
    stm = sorted(s for s in STM_SIGS if s in text)
    return {"unknown_symbols": unknown, "stm": stm}


# --------------------------------------------------- build-failure explanation ----
def summarize_build_failure(output_text):
    """Pull the specific unknown symbols / missing headers out of build output."""
    q = r"[`'\"‘’]?"
    patterns = [
        (re.compile(r"implicit declaration of function " + q + r"([A-Za-z_][A-Za-z0-9_]*)"),
         "unknown function (not in this firmware)"),
        (re.compile(r"undefined reference to " + q + r"([A-Za-z_][A-Za-z0-9_]*)"),
         "unresolved symbol (not built into firmware)"),
        (re.compile(r"unknown type name " + q + r"([A-Za-z_][A-Za-z0-9_]*)"),
         "unknown type"),
        (re.compile(q + r"([A-Za-z_][A-Za-z0-9_]*)" + q + r" undeclared"),
         "undeclared identifier / macro"),
        (re.compile(r"fatal error: ([A-Za-z0-9_./\\-]+\.h(?:pp)?): No such file"),
         "missing header"),
    ]
    found = {}
    for rx, reason in patterns:
        for m in rx.finditer(output_text):
            found.setdefault(m.group(1), reason)
    return found
