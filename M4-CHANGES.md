# Aurora M4 baseline (historical)

This document records the earlier M4/M4 desktop milestone. For the current release, see `M4.5-CHANGES.md` and `README.md`.

M4 refreshes the user-facing startup and desktop shell while keeping the existing freestanding M3 kernel architecture.

## Changes

- Added a short animated-progress boot splash with Aurora branding.
- Added an early-2010s / Windows 7-inspired blue-glass sign-in panel. Press **Enter** or click **Sign in** to continue. The password field is visual only; this prototype does not authenticate users.
- Changed taskbar icon options to 24 px (small), 32 px (medium/default), and 40 px (large).
- Reduced the default taskbar to Start, Search, Files and Notes. Monitor and Settings remain accessible from Start and desktop shortcuts; an app can still appear on the taskbar when running.
- Restyled the taskbar with a transparent cyan tint, glass highlights, pale bevels and brighter running-app indicators while retaining Windows 11-style centered icons.
- Updated the About panel and README to identify the M4 desktop preview.

## Build verification

The freestanding kernel ELF builds with `make build/kernel.elf`. Creating/running the bootable ISO still uses the existing project's documented requirements (`xorriso` and `qemu-system-x86_64`); a graphical VM boot was not performed during this edit.

## Taskbar sizing update
- Increased default taskbar icon size to 32 px (options: 24, 32, 40 px).
- Increased taskbar height and spacing for larger, easier-to-see pinned icons.
- Added a wider two-line clock/date tray, with a 24 px clock font and larger date, centered in the taskbar.
