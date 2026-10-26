# Aurora OS — M4.5.1 desktop preview

Bootable x86-64 freestanding kernel loaded by Limine. This milestone focuses on desktop UI polish and a browser front-end; it is not yet a general-purpose networked OS.

## M4.5.1 resolution control
- Requests **1280 x 720 x 32-bit** as the default Limine framebuffer mode at boot.
- Settings > System offers **1280 x 720**, **1366 x 768**, **1600 x 720**, **1823 x 918**, and **1920 x 1080**.
- Live changes use the Bochs/QEMU VBE DISPI interface when the virtual VGA adapter provides it. The display renderer reallocates its back buffers and rebuilds the wallpaper; the taskbar, windows, and mouse bounds are recalculated after success.
- On adapters without BGA/DISPI support, the Settings page reports that live switching is unavailable. Runtime mode selection is not saved across reboot; 1280 x 720 remains the default boot request.

## M4.5 changes
- **Improved login screen:** layered blue glass, profile identity, Sign in action, and an explicit demo-session notice. Authentication is not implemented in this kernel preview.
- **Windows 11-inspired taskbar:** compact 56 px default height with 32 px icons, centered rounded island, cleaner spacing, and a readable two-line clock/date tray. The 24/32/40 px icon settings remain available.
- **TrueType-derived UI typography:** bundled Go Regular and Go Medium `.ttf` files with BSD-3-Clause license. The regular kernel UI uses a compact antialiased glyph atlas generated from Go Medium TTF, since the current kernel has no filesystem/VFS to load font files at runtime. `tools/ttf2c.py` regenerates `kernel/font_ttf.h` when Pillow is installed.
- **Aurora Browser app:** pinned to the taskbar and Start menu. It opens an address bar prefilled with `https://www.google.com/`, supports editing the address and clicking Go/Enter, and reports secure-network status transparently.

## Browser / TLS limitation (important)
The browser is currently a **UI prototype**, not a working internet browser. M4.5 has no NIC driver, TCP/IP stack, DNS resolver, TLS implementation, or trusted root certificate store. It sends no web requests and does not validate any SSL/TLS certificate. The browser intentionally says “not checked” instead of showing a fake secure lock. Real Google browsing needs those networking and cryptography components before the page can load securely.

## Build and run
Requirements: GCC, make, Python 3. If `xorriso` is installed, the normal Limine ISO recipe is used. Otherwise `tools/mkiso.py` creates an ISO 9660 image with BIOS and UEFI El Torito entries from the bundled Limine assets.

```sh
make
make run
make clean
```

`make run` additionally requires `qemu-system-x86_64` and explicitly uses `-vga std` so the Bochs/QEMU-compatible live mode changer is available. VMware suggestion: “Other 64-bit” VM, BIOS firmware, 2–4 GB RAM, 3D acceleration off, attach `aurora.iso` as the virtual CD/DVD. Live display switching is adapter-dependent and is not guaranteed on VMware SVGA.

## Current shell capabilities
- Aurora wallpaper, desktop icons and RTC clock
- Boot splash and demo login
- Start menu/search, taskbar positioning, icon size personalization, and the M4.5.1 resolution selector
- Window drag/minimize/close/focus
- Files, Notes (not saved), Monitor, Settings, Browser (UI prototype)
- PS/2 keyboard and mouse, Limine boot, framebuffer renderer

## Known limitations
- Browser cannot access the internet yet; no network interface, IP/TCP, DNS, TLS, or CA store.
- Demo login does not authenticate users.
- AuroraFM lists demo data; there is no VFS/filesystem.
- Notes are not saved.
- Input is polled and can keep one host CPU core busy.
- RTC is shown as-is (commonly UTC in QEMU).
- ISO structure is generated and checked locally; a full graphical QEMU/VMware boot has not been run in this environment.

## Licenses
Aurora OS: MIT. Limine: BSD-2-Clause. Go fonts: BSD-3-Clause, see `assets/fonts/LICENSE-Go-Fonts.txt`. Legacy Spleen/font files remain in the source tree for historical reference but are no longer used by the GUI renderer.


### ISO fallback boot notes

When `xorriso` is unavailable, `tools/mkiso.py` creates a plain ISO 9660 image with El Torito BIOS and UEFI entries. The fallback path intentionally does not run `limine bios-install`: that command installs the hard-disk/MBR stage and should only be used on a suitable hybrid image, not as a substitute for the El Torito CD boot record. The El Torito system identifier is NUL-padded as required by the boot record format.


## Resolution safety fix (M4.5.1)

The bootloader is asked to map a 1920x1080 framebuffer aperture, then Aurora
selects 1280x720 before showing its boot console and desktop. The video driver
records the original mapped byte capacity and refuses any mode whose scanout
span would exceed it. This prevents page faults caused by changing the display
controller mode without enlarging the virtual mapping. The desktop default
remains 1280x720 on adapters that support Bochs/QEMU VBE mode switching.


### Transparent Dock Update
The taskbar/dock no longer draws an opaque or translucent bar or rounded floating island. Large icons sit directly over the desktop background. Hover/active states use subtle square highlights, and the clock/date tray stays transparent until hovered.

## Milestone 5.0.2 resolution fix
Limine requests a 1920x1080x32 framebuffer at boot to reserve/map enough framebuffer space for all supported display choices. Aurora then switches the desktop to 1280x720 by default. See `M5.0.2-CHANGES.md`.

## M5.0.3 changes
See `M5.0.3-CHANGES.md` for the window slide/fade-style transitions and taskbar notification flyout. The current low-level renderer supports the upward/downward slide animation and transient window movement; full per-pixel alpha fading of all window contents is not implemented yet.

## Private build: 5.0.3 (james)
See `M5.0.3-JAMES-PRIVATE-CHANGES.md` for kernel hardening notes, including physical memory map validation, contiguous allocator safety, and framebuffer allocation overflow checks.


## Milestone 5.0.4
See `M5.0.4-CHANGES.md` for the text-free, low-blur animated boot splash and hidden normal boot menu.


## Kernel hotfix 5.0.4-b2
This build hardens physical-page freeing by validating that a page belongs to a Limine-reported usable-memory range before returning it to the allocator. It also labels the boot log as 5.0.4-b2.


## Aurora OS 5.1.0 changes

- Kernel startup diagnostics now request and report Limine bootloader name/version when available.
- Required memory-map / HHDM failures give a more actionable panic message.
- Login and system information branding updated to 5.1.0. The login remains a demo-session transition; it is not password authentication.
- Limine menu has a five-second timeout to make recovery and boot selection accessible.
- ISO volume identifiers and boot-catalog label updated to 5.1.0.

Build with `make clean && make`. This freestanding kernel does not yet implement real user accounts or secure login.


## Aurora OS 5.1.0.80001_winxp25 — virtual network discovery

- Added **Aurora Network API v1** (`kernel/net.h`, `kernel/net.c`) with a stable versioned information structure and null-safe query API.
- PCI configuration-space discovery recognizes common Intel E1000/PRO-1000, VirtIO-net, VMware vmxnet3, Realtek RTL8139, and AMD PCnet virtual/network adapters. This is useful for identifying the adapter selected in VirtualBox, VMware, or QEMU.
- Browser status now reports network-adapter discovery honestly instead of implying that a website was loaded. The browser UI and editable address field remain available.
- Updated version strings, Limine menu, ISO volume ID and boot-catalog label.

### Network and browser status (important)
This milestone implements **PCI adapter discovery only**. It does not yet initialize a NIC, send/receive Ethernet frames, acquire DHCP configuration, resolve DNS, implement TCP/HTTP, or validate HTTPS/TLS certificates. Therefore it cannot load live websites yet, and the browser must not be considered a working internet browser. Network API `packet_io_ready` intentionally remains false. Real browsing requires device-specific packet drivers, an IP stack, DNS, TLS, certificate validation, and further testing.

### Virtual machine adapter suggestions
- **VirtualBox:** Intel PRO/1000 MT Desktop (82540EM) is a useful target for the E1000 family.
- **QEMU:** `-device e1000` is a useful target; VirtIO-net discovery is also recognized.
- **VMware:** select an E1000E/E1000-compatible adapter where available; vmxnet3 is recognized by PCI ID but does not yet have a driver.

The ISO is build-checked but has not been boot-tested in VirtualBox, VMware, or QEMU in this environment.

## Aurora OS 5.1.0.01 — browser and virtual display hardening

This revision bounds and validates browser URI input, clips long address-bar text, and safely rejects unsupported URI schemes. It adds Display API v1 to identify common QEMU/Bochs, VirtualBox, VMware, and VirtIO virtual display devices. Rendering still uses the bootloader-provided framebuffer and software renderer; the PCI detector is not a native GPU acceleration driver. The browser remains a UI prototype until packet networking and HTTP/HTTPS/TLS are implemented.


## Aurora OS 5.1.0.03
See `M5.1.0.03-CHANGES.md` for the security API, kernel text diagnostic fingerprint, browser input/render fallback, and explicit TLS limitations. TLS 3.0 is not a standardized protocol; TLS 1.3 is not implemented in this build.

## Aurora OS 5.1.0.03 shell update

The Start menu now has a clearer welcome/pinned-app hierarchy. The notification center uses a bounded six-entry static ring with clear-all and per-item dismissal, and reports browser URI status. AuroraNotes now keeps its buffer length and NUL terminator consistent during typing and backspace. These changes avoid dynamic allocation for shell notifications; they do not imply that all kernel memory leaks have been eliminated.


## Aurora OS 5.1.0.04
See `M5.1.0.04-CHANGES.md` for the Start menu visual refinements, 12-entry bounded notification history, live status display, and notification text lifetime fix. The browser remains a UI prototype without live web transport.


## Aurora OS 5.1.0.15
The login screen and Start menu no longer show welcome greetings. The Start menu's pinned-app section is moved upward to use the freed space. Audited kernel GUI source strings for remaining welcome greetings; none remain in the GUI source. This release is built from the 5.1.0.04 source baseline.


## Aurora OS 5.1.0.15

Kernel hardening release: PMM initialization now rejects missing/empty memory maps, and physical-range alignment has overflow guards. Existing page-free validation for non-usable and double-freed pages is retained. See `M5.1.0.15-CHANGES.md`.


## Aurora OS 5.1.0.15 — platform API and web-readiness honesty

- Added **Aurora Platform Capability API v1** (`kernel/platform_api.h/.c`) with a null-safe capability query and an explicit web-readiness status.
- Kernel boot diagnostics now report implemented platform capabilities and the missing components required for modern web apps.
- Updated GUI, boot-menu, README and ISO volume/version metadata to 5.1.0.15.
- This is API groundwork, not a claim of modern website compatibility: this kernel still lacks packet I/O drivers, TCP/IP, DNS, TLS 1.3 with certificate-chain/hostname validation and a CA store, a userspace application ABI, and a modern browser engine. Do not enter credentials or treat the browser prototype as internet-capable.


## Aurora OS 5.1.0.15-r2

Hardened the network-adapter discovery API with a size-checked query to prevent mismatched caller-buffer sizes from causing structure-copy overruns. API version is now 2; the compatibility wrapper expects the current full structure. This release does not include TCP/IP packet processing; the networking code is PCI discovery only. See `M5.1.0.15-r2-CHANGES.md`.


## Aurora OS 5.1.1.1111111111111111111111111111111.11176 — API and browser input hardening

Platform API v2 adds a size-checked information query. Browser address validation now requires explicit HTTP/HTTPS schemes and rejects malformed authorities before accepting input. This remains input validation only: no web request is sent because packet I/O, TCP/IP, DNS, TLS certificate validation, and a modern browser engine are not implemented. See `M5.1.1.1111111111111111111111111111111.11176-CHANGES.md`.

## Aurora OS 5.1.1.1111111111111111111111111111111.11176 — virtual display and runtime API

Platform capability API v3 reports the implemented software framebuffer renderer and PCI discovery through a size-checked structure. It deliberately does not advertise accelerated VMware/VirtualBox graphics, network transport, a userspace ABI, or modern browser support because those components are not implemented. See `M5.1.1.1111111111111111111111111111111.11176-CHANGES.md`.

## Aurora OS 5.1.1 shell and security refresh
The shell adds a maximize/restore window control and a translucent glass taskbar treatment. Existing security-monitor audit and integrity APIs are retained. This release does not add taskbar icon reordering or hardware-accelerated composition; see `M5.1.1-CHANGES.md`.

## Alpha Developer Stage — Alpha 1.0.0
See `ALPHA-1.0.0-RELEASE-NOTES.md`. This alpha includes five wallpaper presets and a darker translucent taskbar. Live web browsing remains unavailable because network packet I/O, TCP/IP/DNS, TLS, certificate validation, and a user-space Firefox runtime are not implemented.


## Alpha 1.0.0_02 (2026-10-26)
See `ALPHA-1.0.0_02-CHANGES.md` for the kernel/browser build labels and new Messages, Clock, and Maps demo apps.


## Alpha 1.0.0_02 (2026-10-26)
Notes receives a redesigned editor toolbar and File/View flyout menus for creating or clearing a note and toggling the status bar. Messages, Clock, and Maps now use distinct vector icons in the taskbar and Start menu. These apps remain local UI demos; persistence, messaging transport, and live map services are not included.

## Alpha 1.0.2_01a

Fixes desktop shortcut mapping so This PC, Notes, and Settings display and launch their matching applications. Build number: 102620261211.


## Aurora OS Alpha 1.0.2_02

Corrected Notes text spacing by regenerating the kernel glyph atlas from the supplied Arial font and retaining proportional character advances. Build number: 102620261230. See `ALPHA-1.0.2_02-CHANGES.md`.

## Alpha 1.0.3 update
See `ALPHA-1.0.3-CHANGES.md` for Notes proportional text rendering, RGB accent controls, terminal demo, Explorer ribbon controls, API/version changes and known limitations.

## Alpha 1.0.3 Hotfix

This hotfix corrects built-in accent selection overriding the custom RGB color and adds scroll controls to the Settings content pane. Use the fixed up/down controls at the top-right of Settings to navigate long pages.


## Alpha 1.0.4 window animations
Window opening and closing now apply a renderer-wide opacity ramp to the window's chrome, text, icons, and app content, alongside the existing short slide movement. The transition lasts 180 ms. Runtime visual testing is still required. See `ALPHA-1.0.4-CHANGES.md`.

## Alpha 1.0.4_211 (intersec)
This release introduces Shell API v3.0 with a bounded, level-ordered event-handler registry. GUI keyboard and mouse events are dispatched through the system-level handler. Handler levels indicate dispatch priority only; they do not provide privilege isolation.


## Alpha 1.0.5.0

See `ALPHA-1.0.5.0-CHANGES.md` for the Terminal refresh and four added wallpaper designs.

## Alpha 1.0.5.1

Changed the default desktop wallpaper to the newly supplied Nikitas artwork. The image is embedded in RGB565 form for startup use, remains selectable in Settings → Personalization, and the original PNG is included in `/boot/assets`. Updated release branding and build identifier. See `ALPHA-1.0.5.1-CHANGES.md`.

## Alpha 1.0.5.2
Removed Nikitas, Dark Strider, and Windows 11 from the Settings wallpaper picker while retaining the default Nikitas wallpaper and the other selectable presets. The login background now matches the active desktop wallpaper. Browser address handling accepts hostnames, HTTP/HTTPS URLs, and search phrases, but live web access remains unavailable until the kernel has a real network stack and secure HTTP transport. See `ALPHA-1.0.5.2-CHANGES.md`.


## Alpha 1.0.5.3
- Removed unused Nikitas, Dark Strider, and Windows 11 wallpaper image assets from ISO packaging and removed their embedded RGB565 arrays from the kernel.
- Retained the Nikitas (Default) wallpaper and Continuator.
- Resized the medium Start menu to fit five application tiles per row.
- Kept the framebuffer software renderer compatible with boot framebuffers exposed by QEMU, VMware, and VirtualBox. Hardware 2D acceleration is not implemented; choose a standard VGA/compatible graphics adapter if a hypervisor-specific display mode fails.


## Alpha 1.0.6

- Updated the login screen with a medium-opacity dark overlay, keeping the selected desktop wallpaper visible beneath it.
- Reduced bright login-screen accent overlays for improved text and button contrast.
- Rebuilt the kernel and ISO with warning checks.
- Runtime boot and visual verification have not been performed.

## Alpha 1.0.7_01

This update increases Start menu translucency, adds a circular profile avatar beside the login username, and fades the desktop in after Sign in. See `ALPHA-1.0.7-CHANGES.md` for details.

## Alpha 1.0.7_01

Fixes the File Explorer ribbon overlap and adjusts the taskbar clock/date to the compact two-line positioning shown in the reference screenshots. See `ALPHA-1.0.7_01-CHANGES.md`.

## Alpha 1.0.7_02 — TLS and browser integration groundwork
This build packages a CA bundle and exposes TLS API v2 plus a browser capability API. The bundle is not yet loaded into a runtime trust store; certificate validation, TLS handshakes, TCP/IP, DNS, and an HTML/CSS/JavaScript engine remain unimplemented. Consequently this build does not yet support live modern websites or YouTube playback.


## Alpha 1.0.7_03

Removes the Nikitas and Continuator wallpaper assets and embedded image data, sets the supplied landscape image as the default Aurora wallpaper, and changes the login screen to a wallpaper-free lock-screen-style layout with date/time, avatar, and sign-in action. Runtime visual testing remains pending.


## Alpha 1.0.8_02
See `ALPHA-1.0.8-CHANGES.md` for the release notes, input-wheel support, flat full-width taskbar, and API version updates.


## Alpha 1.0.9 — Lock-screen clock refresh
- Updated lock-screen date line to show weekday and date in a compact font.
- Enlarged visual weight of the clock using repeated bitmap-text passes, inspired by the macOS Sonoma lock-screen hierarchy.
- Updated release branding and ISO metadata to Alpha 1.0.9.
- Visual/runtime validation remains a separate step; see release notes.


## Alpha 1.0.10 — Segoe UI font update
- Replaced the previous Arial font asset with the user-supplied `assets/fonts/SegoeUI.ttf`.
- Regenerated the kernel's antialiased proportional glyph atlas from Segoe UI for small, medium, and large UI text, so the kernel-rendered interface uses Segoe UI without needing runtime font-file loading.
- ISO assets now package `SEGOEUI.TTF` and a font notice instead of Arial font files.
- Updated kernel boot branding and ISO volume metadata to `1.0.10`.
- Glyph atlas coverage remains ASCII characters 32–126; this is not a full runtime TrueType renderer.


## Alpha 1.0.10_01 — VMware login rendering hotfix

- Reduced the login wallpaper veil from three full-screen alpha-blended passes to one.
- Preserves the wallpaper-through-grey-glass appearance while reducing software-renderer work during login repaints.
- This is a rendering-cost mitigation; VMware runtime testing is still required to confirm the reported black screen is resolved.


## Alpha 1.0.10_02 — kernel stability pass

- Bounded PCI device discovery to bus 0 for the current virtual-machine targets. The former exhaustive 256-bus scans in both network and display detection caused avoidable slow port-I/O loops during boot. Devices on secondary PCI buses are not discovered by this build.
- Hardened framebuffer validation: reject missing framebuffer mappings, invalid pitch/height arithmetic, and malformed RGB mask layouts before allocating renderer buffers.
- Hardened graphics clipping and rectangle blits against signed overflow and oversized/off-screen rectangles, keeping software rendering loops bounded.
- Retains the 1.0.10_01 single-pass translucent login overlay.
- Build and ISO packaging checks are performed for this release; VMware runtime behavior still requires a VM boot test.


## Alpha 1.0.10_04 — oversized lock-screen clock

- Increased the lock-screen clock to 3x the large bitmap-font size on screens at least 1000 pixels wide (2x on smaller screens).
- Added a bounded scaled-glyph renderer and bold expansion for prominent clock text.
- Kept the date line compact and centered the clock using its scaled width.
- Build verification covers compilation and ISO packaging; VMware runtime behavior remains unverified here.


## Alpha 1.0.11 — Earth liquid-glass Start orb

The Start orb now uses a glossy green and light-blue Earth-inspired palette with green land shapes, a pale glass reflection, the Aurora four-pane logo, and a luminous edge.
