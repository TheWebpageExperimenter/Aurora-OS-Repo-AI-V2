# Aurora OS Alpha 1.0.5.3 changes

- Removed the unused Nikitas, Dark Strider, and Windows 11 image assets from ISO packaging and embedded kernel wallpaper arrays to reduce image size. The Nikitas (Default) wallpaper remains.
- Start menu is medium-sized and fits five app tiles across each row.
- Graphics remains a software framebuffer renderer; QEMU standard VGA, VMware SVGA/VMSVGA, and VirtualBox VGA/VMSVGA are recognized where the bootloader supplies a compatible framebuffer. This release does not add native GPU acceleration or guarantee identical performance across hypervisors.
