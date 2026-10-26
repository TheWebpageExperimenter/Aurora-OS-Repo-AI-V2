# Aurora OS Alpha 1.0.0_01 — Window maximize fix

- Fixed maximize geometry to use the calculated desktop work area, excluding the taskbar on the bottom, left, or right edge.
- Recalculates maximized windows when taskbar orientation changes, preventing a maximized window from remaining off-screen.
- Restores a window to its saved geometry and clamps it back into the current work area.
- Build/ISO label: `alpha-1.0.0_01.iso`.

Runtime boot testing is not claimed unless separately performed.
