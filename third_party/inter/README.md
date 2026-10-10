# Inter

The user-interface sans font, bundled so every platform renders the same.

- `Inter.ttf` from [google/fonts `ofl/inter`](https://github.com/google/fonts/tree/main/ofl/inter)
  (`Inter[opsz,wght].ttf`), the variable font. The build loads the default
  instance, which is Regular (weight 400), because ImGui renders the default
  instance of a variable font.
- `OFL.txt` is the SIL Open Font License 1.1 that covers the font.

The font is compiled into the Windows executable (RCDATA resource) and copied
next to the binary or into the bundle resources on macOS and Linux, so the host
never depends on a system font.
