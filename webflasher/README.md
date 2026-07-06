# Cardputer-ADV Web Flasher

A browser-based flasher for the **M5Stack Cardputer-ADV** (ESP32-S3). No install —
it uses **Web Serial + esptool-js** to flash directly from a Chromium browser.

Flow: **Connect** → pick the serial port → choose **Beta** or **Stable** → watch the
log + progress bar → *"Flash successful"*.

- **Beta** pulls the merged image straight from `Beta_Firmware/Flipper-cardputer_adv-merged.bin`
  on the `Main` branch (raw GitHub URL).
- **Stable** pulls the first `.bin` asset from the repo's **latest GitHub Release**.
  (If you haven't published a release yet, Stable shows a friendly "use Beta" message.)

## Requirements
- **Chrome or Edge on desktop** (Web Serial isn't in Firefox/Safari or mobile).
- Must be served over **HTTPS** (GitHub Pages is HTTPS) or `http://localhost`.

## Host it on GitHub Pages
1. Repo → **Settings → Pages**.
2. **Source:** *Deploy from a branch* → **Branch: `Main`** → **/(root)** → Save.
3. Wait a minute, then open:
   `https://<user>.github.io/Flipper-Zero-meets-M5Stack-Cardputer/webflasher/`

## If you rename the default branch
The Beta/Stable URLs in `index.html` reference the `Main` branch and this repo. If
you rename the branch or move the repo, update the `REPO` / `BRANCH` constants at the
top of the `<script>` in `index.html`.

## Notes
- The Beta image is the full merged binary (bootloader + partition table + app),
  flashed at offset `0x0`.
- Trouble connecting? Hold **G0/BOOT**, tap reset, release, then Connect.
