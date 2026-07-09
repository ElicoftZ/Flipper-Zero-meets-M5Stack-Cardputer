#!/usr/bin/env python3
import os
import sys
import re
import urllib.request
import zipfile
import subprocess
import shutil
import threading
import queue
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

# Paths
if getattr(sys, 'frozen', False):
    EXE_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    EXE_DIR = os.path.dirname(os.path.abspath(__file__))

# Shared, repo-derived compatibility oracle (also used by the FAP Verifier), so a
# single source of truth decides what the port supports.
sys.path.insert(0, EXE_DIR)
try:
    import port_compat
except Exception:
    port_compat = None

if os.path.exists(os.path.join(EXE_DIR, "fam_config.py")):
    ROOT_DIR = EXE_DIR
elif os.path.exists(os.path.join(EXE_DIR, "..", "fam_config.py")):
    ROOT_DIR = os.path.abspath(os.path.join(EXE_DIR, ".."))
else:
    ROOT_DIR = EXE_DIR

APPS_USER_DIR = os.path.join(ROOT_DIR, "applications_user")
FAM_CONFIG_PATH = os.path.join(ROOT_DIR, "fam_config.py")
CONFLICTING_SYMBOLS = ["init", "game_over", "hopping_text", "hopping_value", "tx_power_text"]

# Colors
BG_COLOR = "#020813"
CANVAS_BG = "#050B14"
GRID_COLOR = "#081d33"
NEON_BLUE = "#00bcff"
NEON_BLUE_DARK = "#0a2240"
TEXT_COLOR = "#ffffff"
MUTED_TEXT = "#4f7da3"
GREEN_GLOW = "#00ff66"
RED_GLOW = "#ff2a2a"

class FAPCompilerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("FAP Compiler ADV")
        self.root.geometry("820x620")
        self.root.configure(bg=BG_COLOR)
        
        # Borderless window drag setup
        self.root.overrideredirect(True)
        self._drag_data = {"x": 0, "y": 0}
        
        # Logging Queue
        self.log_queue = queue.Queue()
        self.is_compiling = False
        self.animation_step = 0
        
        self.build_ui()
        self.animate_canvas()
        self.check_queue()
        
    def build_ui(self):
        # Outer Glowing Border
        self.outer_frame = tk.Frame(self.root, bg=NEON_BLUE, bd=1)
        self.outer_frame.pack(fill=tk.BOTH, expand=True)
        
        self.main_container = tk.Frame(self.outer_frame, bg=BG_COLOR)
        self.main_container.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        
        # 1. Custom Title Bar
        self.title_bar = tk.Frame(self.main_container, bg=BG_COLOR, height=35)
        self.title_bar.pack(fill=tk.X)
        self.title_bar.pack_propagate(False)
        
        self.title_bar.bind("<ButtonPress-1>", self.start_move)
        self.title_bar.bind("<ButtonRelease-1>", self.stop_move)
        self.title_bar.bind("<B1-Motion>", self.on_move)
        
        # Title text
        self.title_lbl = tk.Label(self.title_bar, text="FAP COMPILER ADV", fg=NEON_BLUE, bg=BG_COLOR, font=("Consolas", 11, "bold"))
        self.title_lbl.pack(side=tk.LEFT, padx=10)
        
        # Close/Minimize buttons
        self.close_btn = tk.Button(self.title_bar, text="X", fg=TEXT_COLOR, bg=BG_COLOR, activeforeground=RED_GLOW, activebackground=BG_COLOR, bd=0, font=("Consolas", 11, "bold"), command=self.exit_app)
        self.close_btn.pack(side=tk.RIGHT, padx=10)
        
        self.min_btn = tk.Button(self.title_bar, text="_", fg=TEXT_COLOR, bg=BG_COLOR, activeforeground=NEON_BLUE, activebackground=BG_COLOR, bd=0, font=("Consolas", 11, "bold"), command=self.minimize_app)
        self.min_btn.pack(side=tk.RIGHT, padx=5)
        
        # 2. Schematic Blueprint Canvas
        self.canvas_frame = tk.Frame(self.main_container, bg=BG_COLOR, bd=1, highlightbackground=NEON_BLUE_DARK, highlightthickness=1)
        self.canvas_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        self.canvas = tk.Canvas(self.canvas_frame, bg=CANVAS_BG, highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        
        # 3. Input & Action controls (placed inside the Canvas in the middle)
        self.control_frame = tk.Frame(self.canvas, bg=CANVAS_BG)
        
        self.input_border = tk.Frame(self.control_frame, bg=NEON_BLUE_DARK, bd=1)
        self.input_border.pack(side=tk.LEFT, fill=tk.X, expand=True, ipady=3)
        
        self.input_entry = tk.Entry(self.input_border, bg=BG_COLOR, fg=TEXT_COLOR, insertbackground=NEON_BLUE, bd=0, font=("Consolas", 11))
        self.input_entry.pack(fill=tk.X, padx=5)
        self.input_entry.insert(0, "Enter Flipper App GitHub Link, local .zip, or ARM .fap path...")
        self.input_entry.bind("<FocusIn>", self.clear_placeholder)
        self.input_entry.bind("<FocusOut>", self.restore_placeholder)
        
        self.browse_btn = tk.Button(self.control_frame, text="BROWSE", bg=BG_COLOR, fg=NEON_BLUE, activebackground=NEON_BLUE_DARK, activeforeground=TEXT_COLOR, font=("Consolas", 9, "bold"), bd=1, relief=tk.FLAT, command=self.browse_file)
        self.browse_btn.pack(side=tk.LEFT, padx=5)
        
        self.compile_btn = tk.Button(self.control_frame, text="COMPILE & PORT", bg=NEON_BLUE_DARK, fg=TEXT_COLOR, activebackground=NEON_BLUE, activeforeground=BG_COLOR, font=("Consolas", 10, "bold"), bd=1, relief=tk.FLAT, command=self.start_compilation)
        self.compile_btn.pack(side=tk.RIGHT, padx=5)
        
        # Add control frame to the canvas center
        self.canvas.create_window(400, 165, window=self.control_frame, width=760, tags="input_window")
        
        # 4. Collapsible Log Drawer
        self.log_drawer = tk.Frame(self.main_container, bg=BG_COLOR, height=180)
        self.log_drawer.pack(fill=tk.X, padx=10, pady=5)
        self.log_drawer.pack_propagate(False)
        
        self.log_text = tk.Text(self.log_drawer, bg=BG_COLOR, fg=NEON_BLUE, insertbackground=NEON_BLUE, bd=1, relief=tk.SOLID, highlightcolor=NEON_BLUE, font=("Consolas", 9))
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
        # 5. Footer panel
        self.footer = tk.Frame(self.main_container, bg=BG_COLOR, height=45)
        self.footer.pack(fill=tk.X, side=tk.BOTTOM)
        
        # Logs toggle button
        self.logs_toggle = tk.Button(self.footer, text="^ LOGS", fg=NEON_BLUE, bg=BG_COLOR, activebackground=NEON_BLUE_DARK, activeforeground=TEXT_COLOR, bd=1, font=("Consolas", 9, "bold"), command=self.toggle_logs)
        self.logs_toggle.pack(side=tk.LEFT, padx=10, pady=8)
        
        # Status Label
        self.status_lbl = tk.Label(self.footer, text="WAITING FOR INPUT...", fg=MUTED_TEXT, bg=BG_COLOR, font=("Consolas", 10, "bold"))
        self.status_lbl.pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        # Theme toggle button
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
        
    def toggle_logs(self):
        if self.log_drawer.winfo_viewable():
            self.log_drawer.pack_forget()
            self.logs_toggle.configure(text="v LOGS")
        else:
            self.log_drawer.pack(fill=tk.X, padx=10, pady=5)
            self.logs_toggle.configure(text="^ LOGS")
            
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
        self.browse_btn.configure(fg=NEON_BLUE)
        self.compile_btn.configure(bg=NEON_BLUE_DARK)
        self.log_text.configure(fg=NEON_BLUE)
        self.logs_toggle.configure(fg=NEON_BLUE)
        
    def clear_placeholder(self, event):
        if "Enter Flipper App" in self.input_entry.get():
            self.input_entry.delete(0, tk.END)
            
    def restore_placeholder(self, event):
        if not self.input_entry.get().strip():
            self.input_entry.insert(0, "Enter Flipper App GitHub Link, local .zip, or ARM .fap path...")
            
    def browse_file(self):
        fpath = filedialog.askopenfilename(filetypes=[("Flipper Application Packages or Source", "*.fap *.zip")])
        if fpath:
            self.input_entry.delete(0, tk.END)
            self.input_entry.insert(0, fpath)

    def confirm_repo(self, repo_url):
        # Ask on the main thread; block the compiler worker thread until answered.
        # Guards against downloading + compiling an unexpected/malicious repo,
        # especially the auto-located one from a bare .fap-name search.
        result = {"ok": False}
        done = threading.Event()
        def ask():
            try:
                result["ok"] = messagebox.askyesno(
                    "Confirm Repository",
                    "About to DOWNLOAD and COMPILE this source into your firmware:\n\n"
                    f"{repo_url}\n\n"
                    "This builds third-party code as part of your firmware image. "
                    "Only continue if you trust this repository.\n\nProceed with build?",
                    icon="warning", parent=self.root)
            finally:
                done.set()
        self.root.after(0, ask)
        done.wait()
        return result["ok"]
            
    def update_status(self, text, color=TEXT_COLOR):
        self.status_lbl.configure(text=text, fg=color)
        
    def log(self, msg):
        self.log_queue.put(msg)
        
    def check_queue(self):
        while not self.log_queue.empty():
            msg = self.log_queue.get()
            self.log_text.insert(tk.END, msg)
            self.log_text.see(tk.END)
        self.root.after(100, self.check_queue)
        
    def animate_canvas(self):
        self.canvas.delete("anim")
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        if w < 100:  # Canvas not fully initialized yet
            w, h = 800, 320
            
        # Draw background grid lines
        self.canvas.delete("grid")
        for x in range(0, w, 25):
            self.canvas.create_line(x, 0, x, h, fill=GRID_COLOR, tags="grid")
        for y in range(0, h, 25):
            self.canvas.create_line(0, y, w, y, fill=GRID_COLOR, tags="grid")
            
        cx, cy = w // 2, h // 2
        
        # Title text above the input box
        self.canvas.create_text(cx, cy - 80, text="FAP PORTING & COMPILATION ENGINE", fill=NEON_BLUE, font=("Consolas", 15, "bold"), tags="anim")
        
        # Outer input border highlight
        self.canvas.create_rectangle(cx - 384, cy - 25, cx + 384, cy + 25, outline=NEON_BLUE_DARK, width=2, tags="anim")
        
        # Helper text below input box
        self.canvas.create_text(cx, cy + 80, text="ARM CORTEX-M4 FAP -> NATIVE XTENSA ESP32-S3", fill=MUTED_TEXT, font=("Consolas", 9, "bold"), tags="anim")
        
        # Scan animation lines when compiling
        if self.is_compiling:
            self.animation_step = (self.animation_step + 4) % h
            self.canvas.create_line(0, self.animation_step, w, self.animation_step, fill=GREEN_GLOW, width=1, tags="anim")
            self.canvas.create_text(cx, cy - 110, text="PROCESSING...", fill=GREEN_GLOW, font=("Consolas", 10, "bold"), tags="anim")
        else:
            self.canvas.create_text(cx, cy - 110, text="SYSTEM READY", fill=MUTED_TEXT, font=("Consolas", 10, "bold"), tags="anim")
            
        self.root.after(40, self.animate_canvas)
        
    def write(self, txt):
        self.log(txt)
        
    def flush(self):
        pass
        
    def start_compilation(self):
        if self.is_compiling:
            return
            
        repo_url = self.input_entry.get().strip()
        if not repo_url or "Enter Flipper App" in repo_url:
            self.update_status("ERROR: NO INPUT PROVIDED", RED_GLOW)
            return
            
        if not self.log_drawer.winfo_viewable():
            self.toggle_logs()
            
        self.is_compiling = True
        self.compile_btn.configure(state=tk.DISABLED)
        self.log_text.delete("1.0", tk.END)
        self.update_status("STARTING COMPILATION...", NEON_BLUE)
        
        thread = threading.Thread(target=self.run_compiler_flow, args=(repo_url,))
        thread.daemon = True
        thread.start()
        
    def run_compiler_flow(self, url_or_path):
        old_stdout = sys.stdout
        old_stderr = sys.stderr
        sys.stdout = self
        sys.stderr = self
        
        try:
            success = self.execute_compile_logic(url_or_path)
            if success:
                self.update_status("PORTING & COMPILING SUCCESSFUL!", GREEN_GLOW)
            else:
                self.update_status("COMPILATION FAILED", RED_GLOW)
        except Exception as e:
            print(f"\nUnhandled exception in compiler thread: {e}")
            self.update_status("CRITICAL ERROR", RED_GLOW)
        finally:
            sys.stdout = old_stdout
            sys.stderr = old_stderr
            self.is_compiling = False
            self.compile_btn.configure(state=tk.NORMAL)
            
    def execute_compile_logic(self, repo_url):
        print("=" * 60)
        print(" FAP Compiler & Auto-Porting Tool for ESP32 Cardputer")
        print("=" * 60)
        
        repo_url = repo_url.strip('"').strip("'")
        
        if repo_url.lower().endswith(".fap") or ".fap" in repo_url.lower():
            app_name = os.path.splitext(os.path.basename(repo_url.split("?")[0]))[0]
            print(f"Detected ARM FAP input: '{app_name}.fap'")
            
            github_url = self.search_github_source(app_name)
            if github_url:
                print(f"Automatically redirected to source repository: {github_url}")
                repo_url = github_url
            else:
                print(f"Error: Could not automatically locate the source repository for '{app_name}.fap' on GitHub.")
                return False
                
        # Support GitHub deep links: .../tree/BRANCH/sub/folder selects one app in a
        # monorepo. Parse the branch + subfolder; we download the branch and extract
        # only that subfolder.
        gh = port_compat.parse_github(repo_url) if (port_compat and repo_url.startswith("http")) else None
        subpath = gh["subpath"] if gh else ""
        if subpath:
            folder_name = subpath.rstrip("/").split("/")[-1]
        elif gh:
            folder_name = gh["repo"]
        else:
            folder_name = repo_url.rstrip("/").split("/")[-1]
            if folder_name.endswith(".git"):
                folder_name = folder_name[:-4]
            if folder_name.endswith(".zip"):
                folder_name = folder_name[:-4]

        target_dir = os.path.join(APPS_USER_DIR, folder_name)
        print(f"Target directory: {target_dir}")
        if subpath:
            print(f"Selecting subfolder from monorepo: {subpath}")

        # Confirm the resolved source before downloading + compiling it.
        if not self.confirm_repo(repo_url):
            print("Build cancelled: repository not confirmed by user.")
            return False

        zip_path = None
        if repo_url.startswith("http://") or repo_url.startswith("https://"):
            zip_path = self.download_zip_from_github(repo_url, APPS_USER_DIR)
            if not zip_path:
                return False
        else:
            if os.path.exists(repo_url) and repo_url.endswith(".zip"):
                zip_path = os.path.join(APPS_USER_DIR, "temp_archive.zip")
                shutil.copy(repo_url, zip_path)
            else:
                print(f"Error: Invalid input: {repo_url}")
                return False
                
        if not extract_and_move(zip_path, target_dir, subpath):
            return False
            
        # Locate the app manifest. Single-app repos have it at the root; some put
        # the app in a subfolder; monorepos (e.g. flipperzero-good-faps) have MANY.
        # The compiler builds ONE app, so: root -> use it; one in a subfolder ->
        # use it; multiple -> stop and list them (don't dump a whole collection).
        manifest_path = os.path.join(target_dir, "application.fam")
        if not os.path.exists(manifest_path):
            found = []
            for cur, dirs, files in os.walk(target_dir):
                if "application.fam" in files:
                    found.append(os.path.join(cur, "application.fam"))
                    dirs[:] = []
            if len(found) == 1:
                manifest_path = found[0]
                print(f"Found app manifest in subfolder: {os.path.relpath(manifest_path, target_dir)}")
            elif not found:
                print("Error: no application.fam found anywhere in the repository.")
                shutil.rmtree(target_dir, ignore_errors=True)
                return False
            else:
                print(f"This is a monorepo with {len(found)} apps — the compiler builds ONE app.")
                print("Point it at a single app instead (a single-app repo, or a subfolder zip). Apps found:")
                for m in found:
                    print(f"  - {os.path.relpath(os.path.dirname(m), target_dir)}")
                shutil.rmtree(target_dir, ignore_errors=True)
                return False

        appid = parse_appid_from_manifest(manifest_path)
        if not appid:
            print("Error: Could not parse appid from application.fam.")
            return False
            
        print(f"Detected App ID: {appid}")
        
        built_ins = ["input", "notification", "gui", "dialogs", "locale", "cli", "cli_vcp", "storage", 
                     "power", "loader", "desktop", "archive", "about", "bt_settings", "clock", "bad_usb", 
                     "subghz", "passport", "nfc", "infrared", "gpio", "lfrfid", "wlan", "nrf24", "ble_spam", 
                     "js_app", "doom", "wolf3d", "key_copier", "findmy", "totp"]
        
        sanitized_appid = appid.lower()
        if not re.match(r'^[a-z0-9_]+$', sanitized_appid):
            sanitized_appid = re.sub(r'[^a-z0-9_]', '_', sanitized_appid)
            
        if sanitized_appid in built_ins:
            old_sanitized = sanitized_appid
            sanitized_appid = f"{sanitized_appid}_fap"
            print(f"App ID conflict detected with built-in '{old_sanitized}'. Renaming to '{sanitized_appid}'...")
            
        if sanitized_appid != appid:
            print(f"Updating App ID '{appid}' to '{sanitized_appid}' in manifest...")
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest_content = f.read()
            manifest_content = re.sub(r'(appid\s*=\s*["\'])' + re.escape(appid) + r'(["\'])', r'\1' + sanitized_appid + r'\2', manifest_content)
            with open(manifest_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(manifest_content)
            appid = sanitized_appid
            
        # Check and auto-patch TagTinker if detected
        is_tagtinker = (appid == "tagtinker") or os.path.exists(os.path.join(target_dir, "tagtinker_app.c")) or os.path.exists(os.path.join(target_dir, "wifi", "tagtinker_wifi.c"))
        if is_tagtinker:
            print("Auto-patching TagTinker for Cardputer-ADV compatibility...")
            # 1. Update tagtinker_app.c to disable startup Bluetooth stack load to prevent OOM
            app_c_path = os.path.join(target_dir, "tagtinker_app.c")
            if os.path.exists(app_c_path):
                with open(app_c_path, "r", encoding="utf-8") as f:
                    txt = f.read()
                txt = txt.replace("bt_disconnect(app->bt);", "// bt_disconnect(app->bt);")
                txt = txt.replace("bt_profile_restore_default(app->bt);", "// bt_profile_restore_default(app->bt);")
                with open(app_c_path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(txt)

            # 2. Update tagtinker_wifi.c to remove expansion dependency
            wifi_c_path = os.path.join(target_dir, "wifi", "tagtinker_wifi.c")
            if os.path.exists(wifi_c_path):
                with open(wifi_c_path, "r", encoding="utf-8") as f:
                    txt = f.read()
                # Remove include
                txt = txt.replace("#include <expansion/expansion.h>", "// #include <expansion/expansion.h>")
                # Replace structure type
                txt = txt.replace("Expansion*           expansion;", "void*                expansion;")
                # Replace record management & enable/disable in tagtinker_wifi_open
                txt = re.sub(
                    r'/\*\s*Yield the UART from the expansion service before grabbing it\.\s*\*/\s*'
                    r'w->expansion\s*=\s*furi_record_open\(RECORD_EXPANSION\);\s*'
                    r'expansion_disable\(w->expansion\);\s*'
                    r'w->serial\s*=\s*furi_hal_serial_control_acquire\(FuriHalSerialIdUsart\);\s*'
                    r'if\(!w->serial\)\s*{\s*'
                    r'expansion_enable\(w->expansion\);\s*'
                    r'furi_record_close\(RECORD_EXPANSION\);\s*'
                    r'w->expansion\s*=\s*NULL;\s*'
                    r'return\s+false;\s*'
                    r'}',
                    r'w->expansion = NULL;\n    w->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);\n    if(!w->serial) {\n        return false;\n    }',
                    txt
                )
                # Replace expansion close in tagtinker_wifi_close
                txt = txt.replace(
                    "if(w->expansion) {\n        expansion_enable(w->expansion);\n        furi_record_close(RECORD_EXPANSION);\n        w->expansion = NULL;\n    }",
                    "w->expansion = NULL;"
                )
                with open(wifi_c_path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(txt)

            # 3. Rewrite tagtinker_ir.c completely for ESP32
            ir_c_path = os.path.join(target_dir, "ir", "tagtinker_ir.c")
            if os.path.exists(ir_c_path):
                esp32_ir_code = """/*
 * IR transmitter.
 *
 * Rewritten for ESP32-S3 using the native Flipper furi_hal_infrared RMT driver.
 */

#include "tagtinker_ir.h"
#include <furi.h>
#include <furi_hal.h>

static bool ir_initialized = false;
static volatile bool ir_stop_requested = false;

// State structure for the asynchronous RMT feeder callback
typedef struct {
    const uint8_t* data;
    size_t len;
    size_t current_symbol;
    size_t state; // 0 = burst (mark), 1 = gap (space), 2 = final burst (mark), 3 = finished
} TagTinkerIrTxState;

static const uint32_t pp4_gap_us[4] = {
    60,   // symbol 0 ~60 us
    181,  // symbol 1 ~181 us
    121,  // symbol 2 ~121 us
    242,  // symbol 3 ~242 us
};

static FuriHalInfraredTxGetDataState tagtinker_ir_tx_callback(void* context, uint32_t* duration, bool* level) {
    TagTinkerIrTxState* state = context;
    if(state->state == 3) {
        return FuriHalInfraredTxGetDataStateLastDone;
    }

    if(state->state == 0) {
        // Mark (burst)
        *duration = 40; // 40 us burst
        *level = true;
        
        state->state = 1;
        return FuriHalInfraredTxGetDataStateOk;
    } else if(state->state == 1) {
        // Space (gap)
        size_t byte_idx = state->current_symbol / 4;
        size_t sym_idx = state->current_symbol % 4;
        uint8_t current_byte = state->data[byte_idx];
        
        // Shift to get the active symbol
        uint8_t symbol = (current_byte >> (sym_idx * 2)) & 0x03;
        *duration = pp4_gap_us[symbol];
        *level = false;

        state->current_symbol++;
        if(state->current_symbol < state->len * 4) {
            state->state = 0; // go back to burst
        } else {
            state->state = 2; // proceed to final burst
        }
        return FuriHalInfraredTxGetDataStateOk;
    } else if(state->state == 2) {
        // Final closing burst
        *duration = 40;
        *level = true;
        state->state = 3;
        return FuriHalInfraredTxGetDataStateLastDone;
    }

    return FuriHalInfraredTxGetDataStateLastDone;
}

void tagtinker_ir_init(void) {
    ir_initialized = true;
}

void tagtinker_ir_deinit(void) {
    ir_initialized = false;
}

bool tagtinker_ir_transmit(const uint8_t* data, size_t len, uint16_t repeats_raw, uint8_t delay) {
    if(!ir_initialized) return false;
    if(!data || len == 0 || len > 255) return false;

    ir_stop_requested = false;
    uint32_t repeats = repeats_raw & 0x7FFF;

    // Use internal IR transmitter output
    FuriHalInfraredTxPin original_pin = furi_hal_infrared_get_tx_output();
    furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);

    for(uint32_t rep = 0; rep <= repeats; rep++) {
        if(ir_stop_requested) {
            break;
        }

        TagTinkerIrTxState state = {
            .data = data,
            .len = len,
            .current_symbol = 0,
            .state = 0
        };

        furi_hal_infrared_async_tx_set_data_isr_callback(tagtinker_ir_tx_callback, &state);
        // Start transmission with 1.25 MHz carrier and 50% duty cycle
        furi_hal_infrared_async_tx_start(1250000, 0.5f);
        furi_hal_infrared_async_tx_wait_termination();

        if(rep < repeats && delay > 0) {
            furi_delay_ms(delay);
        }
    }

    furi_hal_infrared_set_tx_output(original_pin);
    return true;
}

void tagtinker_ir_stop(void) {
    ir_stop_requested = true;
}
"""
                with open(ir_c_path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(esp32_ir_code)
            
            # 4. Remove expansion from application.fam requirements
            if os.path.exists(manifest_path):
                with open(manifest_path, "r", encoding="utf-8") as f:
                    content = f.read()
                content = content.replace('"expansion"', '').replace("'expansion'", '')
                # Clean up empty elements/commas in requires array
                content = re.sub(r',\s*,', ',', content)
                content = re.sub(r'\[\s*,', '[', content)
                content = re.sub(r',\s*\]', ']', content)
                with open(manifest_path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(content)

        collisions = scan_for_collisions(target_dir)
        update_manifest(manifest_path, collisions)

        register_in_fam_config(appid)

        # Pre-flight: flag furi/HAL calls this port doesn't implement, so the user
        # gets a clear "unknown symbol" list up front instead of a cryptic build
        # error. Heuristic (may miss macros/types) — the build is authoritative,
        # so we warn and continue; a real failure is confirmed + named below.
        print("\n[Compatibility] Checking furi/HAL calls + headers against Cardputer-ADV firmware...")
        app_src_dir = os.path.dirname(manifest_path)
        if port_compat is not None and port_compat.index_available():
            scan = port_compat.scan_source(app_src_dir)
            unknown = scan["unknown_symbols"]
            missing_hdrs = scan["missing_headers"]
            if missing_hdrs:
                print(f"  WARNING: {len(missing_hdrs)} header(s) NOT provided by this port "
                      "(wrong path or not ported):")
                for h in missing_hdrs:
                    print(f"      - #include <{h}>")
            if unknown:
                print(f"  WARNING: {len(unknown)} furi/HAL call(s) NOT found in this firmware "
                      "(this port implements a subset of Flipper's API):")
                for s in unknown:
                    print(f"      - {s}")
            if scan.get("stm"):
                print(f"  WARNING: STM32/ARM-specific code: {', '.join(scan['stm'][:6])}")
            if missing_hdrs or unknown or scan.get("stm"):
                print("  This will most likely FAIL the build. Trying anyway; if it fails, "
                      "the exact cause is confirmed below.")
            else:
                print("  OK: furi/HAL calls + Flipper headers all resolve in this firmware.")
        else:
            print("  (compat module / firmware source unavailable — skipping pre-check)")

        print("\nClean build cache for compilation...")
        cache_dir = os.path.join(ROOT_DIR, f"build_cardputer_adv/esp-idf/main/CMakeFiles/esp32_fam_app_{appid}.dir")
        if os.path.exists(cache_dir):
            shutil.rmtree(cache_dir)
            
        print("Launching compilation process...")

        # Build via the project's real entry point. flipper_build.bat sets up the
        # ESP-IDF environment (idf-python, IDF_TOOLS_PATH, export.bat) and runs
        # winbuild.py, which reconfigures to pick up the app we just registered.
        # (Previously this pointed at an agent scratch dir that does not exist for
        # other users or after that session — a bare idf.py has no IDF env and fails.)
        flipper_build = r"C:\Espressif\flipper_build.bat"
        if os.path.exists(flipper_build):
            cmd = f'"{flipper_build}" cardputer_adv'
        else:
            # Fallback: requires the ESP-IDF env to already be exported on PATH.
            cmd = "python winbuild.py build --board cardputer_adv"

        print(f"Running build tool: {cmd}")
        process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT_DIR)

        output_lines = []
        while True:
            line = process.stdout.readline()
            if not line:
                break
            print(line, end="")
            output_lines.append(line)

        process.wait()
        if process.returncode == 0:
            print("\n" + "=" * 60)
            print(" SUCCESS: APP COMPILED AND ADDED TO FIRMWARE!")
            print("=" * 60)
            return True
        else:
            print("\n" + "=" * 60)
            print(" ERROR: COMPILATION PROCESS RETURNED ERROR.")
            print("=" * 60)
            # Name the exact unknown symbol(s) / missing header(s) that broke it.
            reasons = port_compat.summarize_build_failure("".join(output_lines)) if port_compat else {}
            if reasons:
                print(" REASON: this app needs things the Cardputer-ADV port does NOT provide:")
                for sym, why in sorted(reasons.items()):
                    print(f"   - {sym}   ({why})")
                print(" This port implements a subset of Flipper's furi/HAL API; the items")
                print(" above aren't available here, so the build can't resolve them.")
            else:
                print(" (Could not pinpoint a specific unknown symbol — check the log above.)")
            return False
            
    def search_github_source(self, app_name):
        url = f"https://api.github.com/search/repositories?q={app_name}+flipper"
        print(f"Searching GitHub for source code of '{app_name}'...")
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'AntigravityPortingTool/1.0'})
            with urllib.request.urlopen(req) as response:
                import json
                data = json.loads(response.read().decode('utf-8'))
                items = data.get("items", [])
                if items:
                    return items[0]["html_url"]
        except Exception as e:
            print(f"GitHub search failed: {e}")
        return None
        
    def download_zip_from_github(self, repo_url, dest_dir):
        zip_path = os.path.join(dest_dir, "temp_archive.zip")
        os.makedirs(dest_dir, exist_ok=True)

        # Branch-aware: a /tree/BRANCH/... link downloads that exact branch.
        gh = port_compat.parse_github(repo_url) if port_compat else None
        if gh:
            urls = port_compat.github_zip_urls(gh["owner"], gh["repo"], gh["branch"])
        else:
            base = repo_url.rstrip("/")
            if base.endswith(".git"):
                base = base[:-4]
            urls = [f"{base}/archive/refs/heads/main.zip",
                    f"{base}/archive/refs/heads/master.zip"]

        for zip_url in urls:
            try:
                print(f"Downloading: {zip_url}...")
                req = urllib.request.Request(zip_url, headers={'User-Agent': 'AntigravityPortingTool/1.0'})
                with urllib.request.urlopen(req) as response, open(zip_path, 'wb') as out_file:
                    shutil.copyfileobj(response, out_file)
                return zip_path
            except Exception:
                continue
        print("Failed to retrieve repository zip.")
        return None

def extract_and_move(zip_path, target_dir, subpath=""):
    print("Extracting ZIP archive...")
    extract_temp = os.path.join(target_dir, "_temp_extract")
    os.makedirs(extract_temp, exist_ok=True)
    try:
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(extract_temp)

        contents = os.listdir(extract_temp)
        if not contents:
            return False

        extracted_folder = os.path.join(extract_temp, contents[0])
        if subpath:
            sub = os.path.join(extracted_folder, *subpath.split("/"))
            if not os.path.isdir(sub):
                print(f"Error: subfolder '{subpath}' not found in the repository.")
                shutil.rmtree(extract_temp, ignore_errors=True)
                return False
            extracted_folder = sub
        for item in os.listdir(extracted_folder):
            s = os.path.join(extracted_folder, item)
            d = os.path.join(target_dir, item)
            if os.path.exists(d):
                if os.path.isdir(d):
                    shutil.rmtree(d)
                else:
                    os.remove(d)
            shutil.move(s, d)
        shutil.rmtree(extract_temp)
        os.remove(zip_path)
        return True
    except Exception as e:
        print(f"Extraction failed: {e}")
        return False

def scan_for_collisions(app_dir):
    detected = []
    for root, _, files in os.walk(app_dir):
        for file in files:
            if file.endswith(".c"):
                path = os.path.join(root, file)
                try:
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        content = f.read()
                        for sym in CONFLICTING_SYMBOLS:
                            pattern = r"\b(void|int|bool|char\*|const\s+char\*|uint32_t|int32_t|uint8_t|int8_t)\s+" + re.escape(sym) + r"\b"
                            if re.search(pattern, content) or (sym in ["tx_power_text", "hopping_text", "hopping_value"] and f" {sym}[" in content):
                                if sym not in detected:
                                    detected.append(sym)
                except Exception as e:
                    print(f"Failed to scan {file}: {e}")
    return detected

def patch_sound_defines(app_dir):
    print("Scanning app source files to patch SOUND defines...")
    for root, _, files in os.walk(app_dir):
        for file in files:
            if file.endswith((".c", ".h")):
                path = os.path.join(root, file)
                try:
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        content = f.read()
                    if "#define SOUND" in content:
                        new_content = content.replace("#define SOUND", "//#define SOUND")
                        with open(path, "w", encoding="utf-8", newline="\n") as f:
                            f.write(new_content)
                        print(f"Patched SOUND define in {file}")
                except Exception as e:
                    print(f"Failed to patch SOUND in {file}: {e}")

def update_manifest(manifest_path, collisions):
    with open(manifest_path, "r", encoding="utf-8") as f:
        content = f.read()
        
    modified = False
    
    # 1. Remove music_player from requires
    if "music_player" in content:
        print("Removing music_player dependency from manifest...")
        content = re.sub(r'["\']music_player["\']\s*,?\s*', '', content)
        modified = True
        
        # Patch SOUND define in source files
        app_dir = os.path.dirname(manifest_path)
        patch_sound_defines(app_dir)
        
    # 2. Remove furi from requires
    if "furi" in content:
        print("Removing furi dependency from manifest...")
        content = re.sub(r'["\']furi["\']\s*,?\s*', '', content)
        modified = True

        
    if collisions:
        print(f"Renaming static symbol collisions: {collisions}")
        dir_name = os.path.basename(os.path.dirname(manifest_path))
        safe_dir_name = re.sub(r'[^a-zA-Z0-9_]', '_', dir_name)
        cdefine_overrides = [f'"{sym}={safe_dir_name}_{sym}"' for sym in collisions]
        
        if "cdefines=" in content or "cdefines =" in content:
            pattern = r"(cdefines\s*=\s*\[)(.*?)(\])"
            def repl(match):
                existing = match.group(2).strip()
                connector = ", " if existing else ""
                return f"{match.group(1)}{existing}{connector}{', '.join(cdefine_overrides)}{match.group(3)}"
            content = re.sub(pattern, repl, content, flags=re.DOTALL)
        else:
            pattern = r"(App\()"
            replacement = f"App(\n    cdefines=[{', '.join(cdefine_overrides)}],"
            content = re.sub(pattern, replacement, content, 1)
        modified = True
        
    if modified:
        with open(manifest_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
        print("Manifest overrides successfully applied.")


def register_in_fam_config(appid):
    with open(FAM_CONFIG_PATH, "r", encoding="utf-8") as f:
        content = f.read()
        
    if f'"{appid}"' in content or f"'{appid}'" in content:
        print(f"App '{appid}' is already registered in fam_config.py.")
        return
        
    pattern = r"(APPS\s*=\s*\[)(.*?)(\])"
    match = re.search(pattern, content, re.DOTALL)
    if not match:
        print("Error: Could not locate APPS list in fam_config.py")
        return
        
    apps_content = match.group(2)
    new_app_entry = f'\n    "{appid}",'
    new_apps_content = apps_content.rstrip() + new_app_entry + "\n"
    new_content = content[:match.start()] + f"APPS = [{new_apps_content}]" + content[match.end():]
    
    with open(FAM_CONFIG_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(new_content)
    print(f"Registered '{appid}' in fam_config.py.")

def parse_appid_from_manifest(manifest_path):
    with open(manifest_path, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r'appid\s*=\s*["\']([^"\']+)["\']', content)
    return match.group(1) if match else None

# API-compatibility scanning + build-failure parsing now live in the shared
# port_compat module (imported above), used by BOTH the Compiler and the Verifier.

if __name__ == "__main__":
    root = tk.Tk()
    app = FAPCompilerGUI(root)
    root.mainloop()
