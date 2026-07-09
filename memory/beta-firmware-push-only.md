---
name: beta-firmware-push-only
description: "After a new firmware build, push ONLY the Beta_Firmware/ folder to GitHub (not source) until the user explicitly says \"push all files\""
metadata: 
  node_type: memory
  type: feedback
  originSessionId: fa388d06-0ff9-46aa-814c-219c49a565d4
---

When a new firmware `.bin` is built for this project (`C:\Users\Eli\Downloads\Flipper`) and the user wants it on GitHub, push **only** the `Beta_Firmware/` folder — the merged `Flipper-cardputer_adv-merged.bin` plus its `README.md` / `PIN_CONFIG.md`. Do NOT push the source-code changes or any other working-tree files.

**Why:** The user keeps source changes local / in-progress and only wants the downloadable firmware artifact (and its docs) published for their phone and friends. They decide when the full source gets pushed.

**How to apply:** Commit only the Beta_Firmware paths with an explicit pathspec so other staged/modified files are left untouched: `git commit -- Beta_Firmware/...` then `git push origin Main` (remote `origin` = ElicoftZ/Flipper-Zero-meets-M5Stack-Cardputer, branch `Main`). The merged bin is caught by the `*-merged.bin` rule in `.gitignore`, so it needs `git add -f` (first add only; once tracked, plain `git add` picks up updates). Only push the source / everything when the user explicitly says **"push all files"**. Related: [[flipper-cardputer-adv-fap-apps-quirks]].

**Beta_Firmware desktop tools (2026-07-06):** the FAP **Verifier** (`Beta_Firmware/FAPVerifier.exe` ← `FAPVerifier/fap_verifier.py`) and FAP **Compiler** (`Beta_Firmware/FAPCompiler.exe` ← `fap_compiler.py`) now BOTH import a shared oracle **`Beta_Firmware/port_compat.py`** — the single source of truth for "what the port supports." It derives the supported set LIVE from the firmware headers (741 furi/HAL symbols + all Flipper headers), cached in the OS temp dir keyed on a fingerprint of header mtimes/sizes + git HEAD (so it auto-updates when new furi support is added and never goes stale → no false positives). The Verifier's old STM/ARM "Hardware" check is now **"Compatibility"** (unsupported furi/HAL calls + missing/unported headers + STM/ARM). Edit compat logic in `port_compat.py` only. Rebuild each exe with PyInstaller `--onefile --windowed --hidden-import port_compat --paths <dir-of-port_compat>`; the Verifier exe MUST land at the canonical **root** `Beta_Firmware/FAPVerifier.exe` (not the `FAPVerifier/` subdir). Both use a `--check <url|zip|fap>` CLI. Fixed-cost gotcha: header check skips bare/`esp*`/IDF-namespaced includes and honors the `lib/` include root to avoid false positives.