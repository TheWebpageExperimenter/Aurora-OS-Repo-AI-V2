Aurora OS Alpha 1.0.10_02 — kernel stability pass

- Reduced boot-time PCI probing from an exhaustive 256-bus sweep to bus 0 for the current VMware/QEMU/VirtualBox virtual hardware targets. This trades secondary-bus discovery for bounded startup work.
- Added stricter framebuffer address, pitch/height overflow, and RGB mask validation before renderer setup.
- Hardened clip and blit rectangle bounds with 64-bit intermediate arithmetic.
- Preserved the 1.0.10_01 login overlay rendering mitigation.
- Build and ISO packaging checks only; not yet runtime-tested in VMware.
