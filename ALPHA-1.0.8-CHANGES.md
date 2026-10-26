# Aurora OS Alpha 1.0.8

- Updated kernel/platform API to 2.2, Shell API to 4.0, browser capability API to v2, and taskbar style API to v3.
- Added PS/2 IntelliMouse wheel negotiation with fallback to standard three-byte mouse packets. Wheel deltas are bounded and exposed through `ps2_take_wheel()`.
- Mouse-wheel scrolling now works in Settings, Explorer's file list, Notes, and the browser preview content.
- Replaced the floating rounded taskbar with a rectangular, edge-to-edge taskbar that spans the screen edge.
- Updated browser and About/terminal version strings.
- Hardened input handling with packet resynchronization and bounded wheel deltas.

## Limitations and validation

The browser remains a preview UI: the kernel still does not provide a complete TCP/IP + DNS + TLS transport or HTML/CSS/JavaScript engine, so modern websites and YouTube are not supported by this build. Compilation and ISO packaging were checked; runtime testing on physical hardware and multiple hypervisors has not been performed.
