# Aurora OS Alpha 1.0.4_211 (intersec)

- Added Shell API v3 with a bounded event-handler registry.
- Added handler levels (kernel, system, application) and ordered event dispatch.
- Routed GUI mouse and keyboard events through the Shell API dispatch path.
- Updated boot/desktop version branding and ISO volume label.

## Scope and limitations
- This is a static, in-kernel event dispatcher, not a process-isolated user-space shell.
- Handler levels provide dispatch priority only; they are not a security boundary or privilege mechanism.
- Existing window fade animations and desktop applications are retained.

## Validation
- Build and ISO generation results are recorded with the release artifacts.
- Runtime boot/UI testing must be performed separately.
