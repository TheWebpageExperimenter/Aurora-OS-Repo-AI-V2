# Aurora OS Alpha 1.0.4 — Window fade animations

- Added renderer-wide opacity modulation for primitives, including text and icons.
- Window opening fades from transparent to opaque while moving slightly upward.
- Window closing fades out while moving slightly upward, then the window is removed after 180 ms.
- Kept the existing window manager, hit testing, and close lifecycle.

## Validation
- `make clean && make -j2` completed successfully.
- No compiler warning/error lines were present in the captured build output.
- ISO generated with updated `AURORA_104` volume label.
- Runtime GUI animation was not tested in QEMU/VMware.
