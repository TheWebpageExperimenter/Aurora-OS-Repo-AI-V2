# Aurora OS Alpha 1.0.2_02 — Notes typography fix

- Regenerated the embedded antialiased glyph atlas from the supplied `assets/fonts/Arial.ttf` rather than leaving the previous Go Medium atlas in the kernel.
- Preserved per-character Arial advances so Notes text uses normal proportional spacing instead of the visibly incorrect spacing in Alpha 1.0.2_01a.
- Updated kernel build string and boot-menu branding to Alpha 1.0.2_02 (build 102620261230).

## Scope and validation

The current renderer uses an embedded rasterized glyph atlas for printable ASCII characters (U+0020 through U+007E); it is not a full runtime TrueType shaping engine. The Notes editor's text layout uses the corrected proportional advances. Kernel compilation and ISO generation were checked; a graphical runtime test was not performed.
