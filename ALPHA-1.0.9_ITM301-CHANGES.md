# Aurora OS Alpha 1.0.9_ITM301

- Replaced Arial with the user-supplied Segoe UI TrueType font.
- Regenerated `kernel/font_ttf.h` from Segoe UI at the existing small, medium, and large UI sizes, retaining proportional character advances and antialiased coverage.
- Replaced the ISO font asset with `SEGOEUI.TTF` and added a notice about font licensing.
- Updated boot/kernel release branding and ISO metadata.

The GUI uses a pre-rasterized embedded glyph atlas because there is no runtime font loader/VFS yet. Current atlas coverage is ASCII 32–126. Build and archive checks are performed separately; runtime visual testing is not implied.
