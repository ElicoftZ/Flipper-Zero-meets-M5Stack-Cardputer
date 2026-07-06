#!/usr/bin/env python3
"""
FAP Manager ADV — list and remove user apps from the Cardputer-ADV firmware repo.

Complements FAPCompiler (which ADDS apps). It auto-detects the repo, lists every
app under applications_user/, and safely DELETES one on request: it removes the
app's folder AND unregisters its appid from fam_config.py's APPS list — doing only
one of those breaks the next build (CMake KeyError on a stale appid, or a dangling
manifest). Core apps in applications/ and components/ are intentionally NOT touched.
"""
import os
import sys
import re
import shutil
import tkinter as tk
from tkinter import messagebox

if getattr(sys, "frozen", False):
    EXE_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    EXE_DIR = os.path.dirname(os.path.abspath(__file__))


def find_root():
    for cand in (EXE_DIR, os.path.join(EXE_DIR, ".."), os.path.join(EXE_DIR, "..", "..")):
        if os.path.exists(os.path.join(cand, "fam_config.py")):
            return os.path.abspath(cand)
    return os.path.abspath(EXE_DIR)


ROOT_DIR = find_root()
APPS_USER_DIR = os.path.join(ROOT_DIR, "applications_user")
FAM_CONFIG_PATH = os.path.join(ROOT_DIR, "fam_config.py")

# Theme (matches FAPVerifier / FAPCompiler)
BG_COLOR = "#020813"
CANVAS_BG = "#050B14"
GRID_COLOR = "#081d33"
NEON_BLUE = "#00bcff"
NEON_BLUE_DARK = "#0a2240"
TEXT_COLOR = "#ffffff"
MUTED_TEXT = "#4f7da3"
GREEN_GLOW = "#00ff66"
RED_GLOW = "#ff2a2a"


# ---------------------------------------------------------------- repo logic ----
def _field(manifest_path, field):
    try:
        with open(manifest_path, "r", encoding="utf-8", errors="ignore") as f:
            c = f.read()
    except Exception:
        return None
    m = re.search(field + r'\s*=\s*["\']([^"\']+)["\']', c)
    return m.group(1) if m else None


def scan_apps():
    """Return the deletable user apps: each top-level folder under applications_user/
    that contains an application.fam (at any depth)."""
    apps = []
    if not os.path.isdir(APPS_USER_DIR):
        return apps
    for entry in sorted(os.listdir(APPS_USER_DIR), key=str.lower):
        top = os.path.join(APPS_USER_DIR, entry)
        if not os.path.isdir(top):
            continue
        manifest = None
        for cur, _dirs, files in os.walk(top):
            if "application.fam" in files:
                manifest = os.path.join(cur, "application.fam")
                break
        if not manifest:
            continue
        appid = _field(manifest, "appid") or entry
        name = _field(manifest, "name") or appid
        apps.append({"name": name, "appid": appid, "dir": top})
    return apps


def registered_appids():
    try:
        with open(FAM_CONFIG_PATH, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception:
        return set()
    m = re.search(r"APPS\s*=\s*\[(.*?)\]", content, re.DOTALL)
    if not m:
        return set()
    return set(re.findall(r'["\']([^"\']+)["\']', m.group(1)))


def unregister_from_fam_config(appid):
    """Remove `"appid",` from the APPS list only (not other references)."""
    try:
        with open(FAM_CONFIG_PATH, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception:
        return False
    m = re.search(r"(APPS\s*=\s*\[)(.*?)(\])", content, re.DOTALL)
    if not m:
        return False
    block = m.group(2)
    new_block = re.sub(r'[ \t]*["\']' + re.escape(appid) + r'["\']\s*,?[ \t]*\r?\n', "", block)
    if new_block == block:
        return False
    new_content = content[:m.start()] + m.group(1) + new_block + m.group(3) + content[m.end():]
    with open(FAM_CONFIG_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(new_content)
    return True


def delete_app(app):
    """Delete the app folder and unregister its appid. Returns (ok, message)."""
    removed_dir = False
    if os.path.isdir(app["dir"]):
        try:
            shutil.rmtree(app["dir"])
            removed_dir = True
        except Exception as e:
            return False, f"Could not delete folder: {e}"
    unregistered = unregister_from_fam_config(app["appid"])
    # Also drop this app's build cache so the next build is clean.
    cache = os.path.join(ROOT_DIR, "build_cardputer_adv", "esp-idf", "main",
                         "CMakeFiles", f"esp32_fam_app_{app['appid']}.dir")
    if os.path.isdir(cache):
        shutil.rmtree(cache, ignore_errors=True)
    parts = []
    parts.append("folder removed" if removed_dir else "folder was already gone")
    parts.append("unregistered from fam_config" if unregistered else "was not in APPS")
    return True, "; ".join(parts)


# --------------------------------------------------------------------- GUI ----
class FAPManagerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("FAP Manager ADV")
        self.root.geometry("560x600")
        self.root.configure(bg=BG_COLOR)
        self.root.overrideredirect(True)
        self._drag = {"x": 0, "y": 0}
        self.build_ui()
        self.refresh()

    def build_ui(self):
        outer = tk.Frame(self.root, bg=NEON_BLUE, bd=1)
        outer.pack(fill=tk.BOTH, expand=True)
        main = tk.Frame(outer, bg=BG_COLOR)
        main.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)

        bar = tk.Frame(main, bg=BG_COLOR, height=35)
        bar.pack(fill=tk.X)
        bar.pack_propagate(False)
        bar.bind("<ButtonPress-1>", self._start)
        bar.bind("<B1-Motion>", self._move)
        tk.Label(bar, text="FAP MANAGER ADV", fg=NEON_BLUE, bg=BG_COLOR,
                 font=("Consolas", 11, "bold")).pack(side=tk.LEFT, padx=10)
        tk.Button(bar, text="X", fg=TEXT_COLOR, bg=BG_COLOR, activeforeground=RED_GLOW,
                  activebackground=BG_COLOR, bd=0, font=("Consolas", 11, "bold"),
                  command=lambda: sys.exit(0)).pack(side=tk.RIGHT, padx=10)
        tk.Button(bar, text="_", fg=TEXT_COLOR, bg=BG_COLOR, activeforeground=NEON_BLUE,
                  activebackground=BG_COLOR, bd=0, font=("Consolas", 11, "bold"),
                  command=self.root.iconify).pack(side=tk.RIGHT, padx=5)

        tk.Label(main, text="INSTALLED USER APPS", fg=NEON_BLUE, bg=BG_COLOR,
                 font=("Consolas", 14, "bold")).pack(pady=(6, 0))
        self.repo_lbl = tk.Label(main, text=ROOT_DIR, fg=MUTED_TEXT, bg=BG_COLOR,
                                 font=("Consolas", 8))
        self.repo_lbl.pack()

        top = tk.Frame(main, bg=BG_COLOR)
        top.pack(fill=tk.X, padx=10, pady=6)
        tk.Button(top, text="RESCAN", bg=NEON_BLUE_DARK, fg=TEXT_COLOR,
                  activebackground=NEON_BLUE, activeforeground=BG_COLOR, bd=1, relief=tk.FLAT,
                  font=("Consolas", 9, "bold"), command=self.refresh).pack(side=tk.LEFT)
        self.count_lbl = tk.Label(top, text="", fg=TEXT_COLOR, bg=BG_COLOR,
                                  font=("Consolas", 9, "bold"))
        self.count_lbl.pack(side=tk.RIGHT)

        # scrollable list
        wrap = tk.Frame(main, bg=CANVAS_BG, highlightbackground=NEON_BLUE_DARK, highlightthickness=1)
        wrap.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 6))
        self.canv = tk.Canvas(wrap, bg=CANVAS_BG, highlightthickness=0)
        sb = tk.Scrollbar(wrap, orient="vertical", command=self.canv.yview)
        self.list_frame = tk.Frame(self.canv, bg=CANVAS_BG)
        self.list_frame.bind("<Configure>",
                             lambda e: self.canv.configure(scrollregion=self.canv.bbox("all")))
        self.canv.create_window((0, 0), window=self.list_frame, anchor="nw", width=505)
        self.canv.configure(yscrollcommand=sb.set)
        self.canv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.canv.bind("<Enter>", lambda e: self.canv.bind_all(
            "<MouseWheel>", lambda ev: self.canv.yview_scroll(int(-ev.delta / 120), "units")))
        self.canv.bind("<Leave>", lambda e: self.canv.unbind_all("<MouseWheel>"))

        self.status = tk.Label(main, text="READY", fg=MUTED_TEXT, bg=BG_COLOR,
                               font=("Consolas", 9, "bold"))
        self.status.pack(fill=tk.X, side=tk.BOTTOM, pady=6)

    def _start(self, e):
        self._drag = {"x": e.x, "y": e.y}

    def _move(self, e):
        self.root.geometry(f"+{self.root.winfo_x() + e.x - self._drag['x']}"
                           f"+{self.root.winfo_y() + e.y - self._drag['y']}")

    def refresh(self):
        for w in self.list_frame.winfo_children():
            w.destroy()
        if not os.path.exists(FAM_CONFIG_PATH):
            self.count_lbl.config(text="")
            self.status.config(text="ERROR: fam_config.py not found — put this next to the repo",
                               fg=RED_GLOW)
            tk.Label(self.list_frame, text="Repo not found.", fg=RED_GLOW, bg=CANVAS_BG,
                     font=("Consolas", 10, "bold")).pack(pady=20)
            return
        apps = scan_apps()
        reg = registered_appids()
        self.count_lbl.config(text=f"{len(apps)} apps")
        if not apps:
            tk.Label(self.list_frame, text="No user apps in applications_user/.",
                     fg=MUTED_TEXT, bg=CANVAS_BG, font=("Consolas", 10)).pack(pady=20)
        for app in apps:
            self._app_row(app, app["appid"] in reg)
        self.status.config(text=f"Scanned {ROOT_DIR}", fg=GREEN_GLOW)

    def _app_row(self, app, is_registered):
        row = tk.Frame(self.list_frame, bg=BG_COLOR, highlightbackground=NEON_BLUE_DARK,
                       highlightthickness=1)
        row.pack(fill=tk.X, pady=3, padx=3)
        info = tk.Frame(row, bg=BG_COLOR)
        info.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=6, pady=4)
        tk.Label(info, text=app["name"], fg=TEXT_COLOR, bg=BG_COLOR,
                 font=("Consolas", 10, "bold"), anchor="w").pack(fill=tk.X)
        tag = "" if is_registered else "  (not in APPS)"
        tk.Label(info, text=f"appid: {app['appid']}{tag}", fg=MUTED_TEXT, bg=BG_COLOR,
                 font=("Consolas", 8), anchor="w").pack(fill=tk.X)
        tk.Label(info, text=os.path.relpath(app["dir"], ROOT_DIR), fg=MUTED_TEXT, bg=BG_COLOR,
                 font=("Consolas", 7), anchor="w").pack(fill=tk.X)
        tk.Button(row, text="DELETE", bg="#3a0a0a", fg=RED_GLOW, activebackground=RED_GLOW,
                  activeforeground=BG_COLOR, bd=1, relief=tk.FLAT, font=("Consolas", 9, "bold"),
                  command=lambda a=app: self._delete(a)).pack(side=tk.RIGHT, padx=8, pady=8)

    def _delete(self, app):
        ok = messagebox.askyesno(
            "Delete app",
            f"Delete this app from the firmware?\n\n"
            f"Name:  {app['name']}\n"
            f"appid: {app['appid']}\n"
            f"Folder: {os.path.relpath(app['dir'], ROOT_DIR)}\n\n"
            "This removes the folder AND unregisters it from fam_config.py. "
            "Rebuild the firmware to apply.\n\nProceed?",
            icon="warning", parent=self.root)
        if not ok:
            return
        success, msg = delete_app(app)
        self.status.config(text=(f"Deleted {app['appid']}: {msg}" if success else msg),
                           fg=(GREEN_GLOW if success else RED_GLOW))
        self.refresh()


def main():
    root = tk.Tk()
    FAPManagerGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
