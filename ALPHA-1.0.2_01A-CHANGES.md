# Aurora OS Alpha 1.0.2_01a — Arial system font update

- Rebuilt the embedded antialiased glyph atlas from the user-supplied `arial.ttf`.
- Bundled the supplied font in the boot assets and made the ISO font asset names explicitly identify Arial.
- Existing GUI drawing calls use the shared glyph atlas, so the replacement applies across kernel-rendered desktop UI text.
- This is a bitmap atlas generated from Arial at three fixed sizes, not a full dynamic TrueType font engine; glyph coverage is currently ASCII 32–126.
- Runtime visual validation was not performed.
