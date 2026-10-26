# Aurora OS Alpha 1.0.5.1 — Default Wallpaper and Kernel Checks

- Changed the default desktop wallpaper to the newly supplied Nikitas artwork.
- Kept the previous nine wallpaper choices and added the new image as the tenth option, `Nikitas (Default)`, in Settings → Personalization.
- Embedded a 512×288 RGB565 version in the kernel wallpaper asset header so the default works without a runtime filesystem driver.
- Included the original PNG in source assets and `/boot/assets/NIKITAS-DEFAULT.PNG` for reuse.
- Updated Limine menu, ISO volume label, and kernel startup branding to Alpha 1.0.5.1.
- Validation focuses on compilation diagnostics, kernel/ISO generation, ISO metadata, and archive integrity. Runtime boot testing requires a VM/hardware environment and is not implied by a successful build.
