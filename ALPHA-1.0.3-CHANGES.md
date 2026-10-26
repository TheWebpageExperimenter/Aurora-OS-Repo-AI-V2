# Aurora OS Alpha 1.0.3

## Implemented in this source/build
- Fixed Notes character placement to use each glyph's actual font-atlas advance width rather than a fixed-width cell. Cursor placement now follows proportional advances and wraps at the editor's right edge.
- Added RGB accent adjustment controls in Settings (R/G/B decrease/increase controls, 16-step increments) and a live color preview; default accent remains blue.
- Added 800x600 and 1280x720 choices to the display-resolution settings list (subject to framebuffer/firmware mode support).
- Added Aurora Terminal to the application list, Start menu and taskbar pins, with a procedural terminal icon.
- Added basic in-kernel terminal commands: `help`, `ver`, `fastfetch`, `uname`, `ls`, and `clear`. These are shell demo commands, not a Linux/POSIX compatibility layer.
- `ver` opens an About-style panel with Aurora logo, release/kernel API, build number, date and profile.
- Added Explorer ribbon-style Edit/Rename/Delete controls and status feedback. File operations remain demo-only because writable AuroraFS/VFS is not implemented.
- Bumped taskbar API version constant to 2 and updated release branding to Alpha 1.0.3, shell 2.0, browser label 1.0.3-alpha.

## Limitations
- RGB controls adjust channels in 16-step increments; this is not a free-form graphical color wheel or direct numeric RGB entry.
- Network stack/connection behavior is not fully implemented; this build does not claim working general internet connectivity.
- Event dispatch remains the existing kernel-resident GUI event loop; this release does not add a separate interrupt-driven user-space event subsystem.
- Hardware crash fixes and display modes require runtime testing on the target machine; no real-hardware validation was performed.
- `fastfetch` and terminal filesystem output are simulated summaries, not a Linux shell or real filesystem tools.
