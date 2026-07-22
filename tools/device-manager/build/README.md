# `build/` — electron-builder asset directory

electron-builder's own conventional location for platform-specific build
assets (icons, installer graphics). **Empty by design** — see
`Docs/DESKTOP_PACKAGING.md`'s "Icon support" section for the full reasoning.

Summary: no icon asset has been added here. Designing an application icon
is a design deliverable, not an engineering one — this project's own
established discipline (`certs.h`'s placeholder CA certificate, `LICENSE`'s
placeholder) is to never fabricate a real asset/decision that isn't this
session's to make, and an icon is squarely in that category.

## What to add, when a real icon exists

1. `build/icon.ico` — Windows icon, **required**: a multi-resolution `.ico`
   containing at minimum 16×16, 32×32, 48×48, and 256×256 sizes (Windows
   selects the appropriate size per context — taskbar, Explorer, installer
   — from a single multi-res file; a single-resolution `.ico` will look
   wrong in most of those contexts).
2. Once `build/icon.ico` exists, add one line to
   `tools/device-manager/package.json`'s `build.win` section:
   ```json
   "win": {
     "target": ["nsis", "portable"],
     "executableName": "CovioDeviceManager",
     "icon": "build/icon.ico"
   }
   ```
   Without this line, electron-builder uses its own generic default icon —
   a real, always-available fallback, not a broken build. This is
   deliberate: setting `icon` to a path that doesn't exist would make
   `npm run dist` fail outright, which would have made it impossible to
   actually verify this phase's packaging configuration works at all
   (`Docs/DESKTOP_PACKAGING.md` records that a real `npm run dist` was run
   and verified during this phase).
