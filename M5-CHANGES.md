# Aurora OS Milestone 5

## Included
- Personalization controls for three built-in placeholder wallpapers: Aurora, Blue Horizon, Neon Bloom.
- Wallpaper mode selector: Full screen (default), Fit, Stretch, Center, Tile.
- Selected preset redraws immediately. Presets are procedural placeholder gradients, not the supplied photographs.
- Existing Limine framebuffer graphics remain the display path in virtual machines.

## Sound and VM compatibility status
This milestone does **not** claim complete VMware/VirtualBox sound support. The current kernel has no PCI audio bus-master driver, AC'97/Intel HDA codec driver, or full audio mixer/output stack. VM display output uses the Limine-provided framebuffer; this is not equivalent to native VMware SVGA or VirtualBox VMSVGA accelerated drivers. Those require separate driver work and testing with the respective virtual hardware.

## Wallpaper mode note
The mode options are exposed in Settings. Since the current wallpapers are generated at the display's full canvas size, Full screen/Fit/Stretch/Center/Tile currently share full-canvas rendering. Scaling modes for external image files remain future work.
