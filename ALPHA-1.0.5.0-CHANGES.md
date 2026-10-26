# Aurora OS Alpha 1.0.5.0 — Changes

- Redesigned Aurora Terminal with a dark session header, status indicators, dedicated output panel, and highlighted command prompt.
- Added four user-supplied wallpapers to Settings → Personalization: Nikitas, Dark Strider, Windows 11, and The Continuator.
- Embedded downscaled RGB565 copies of the four wallpapers in the kernel graphics assets so the images are available at runtime without a filesystem driver.
- Included the original PNG artwork in the ISO's `/boot/assets` directory for reuse.
- Updated release branding and terminal version labels to Alpha 1.0.5.0.

Limitations: Terminal commands remain built-in demo commands, not a full Linux shell. Wallpaper images are nearest-neighbor scaled to the active framebuffer; wallpaper fit/tile modes do not yet apply individually to these four embedded images. Runtime visual boot testing has not been performed.
