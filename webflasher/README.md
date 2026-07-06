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
This repo is ~500 MB, so the default "Deploy from a branch" mode fails at the deploy
step. Use the included Actions workflow (`.github/workflows/deploy-pages.yml`), which
publishes **only this folder**:

1. Repo → **Settings → Pages** → **Source: `GitHub Actions`** (NOT "Deploy from a branch").
2. Push to `Main` (or run the *Deploy web flasher to Pages* workflow manually from the
   Actions tab). It deploys in ~30 s.
3. Open the Pages URL (the flasher is at the site **root**):
   `https://<user>.github.io/Flipper-Zero-meets-M5Stack-Cardputer/`

## If you rename the default branch
The Beta/Stable URLs in `index.html` reference the `Main` branch and this repo. If
you rename the branch or move the repo, update the `REPO` / `BRANCH` constants at the
top of the `<script>` in `index.html`.

## Notes
- The Beta image is the full merged binary (bootloader + partition table + app),
  flashed at offset `0x0`.
- Trouble connecting? Hold **G0/BOOT**, tap reset, release, then Connect.
