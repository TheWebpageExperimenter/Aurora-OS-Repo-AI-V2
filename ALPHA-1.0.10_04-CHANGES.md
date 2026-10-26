# Aurora OS Alpha 1.0.10_04 — Font quality fix

- Regenerated the embedded antialiased font atlas from the bundled Segoe UI TrueType font rather than the previous Go Medium source.
- The lock-screen clock and the rest of the UI now use the same Segoe UI-derived glyph family, avoiding the mismatched serif-like appearance in the lock-screen clock.
- Kept the oversized, bold lock-screen clock layout introduced in Alpha 1.0.10_03.
- Build target: x86-64 bootable ISO.

Validation: kernel compiled and ISO packaging completed. QEMU/VMware runtime appearance has not been independently tested in this environment.
