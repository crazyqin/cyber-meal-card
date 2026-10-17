<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

The subscription viewer uses `fonts/quota/NotoSansCJKsc-Regular.otf` from
[Noto CJK](https://github.com/notofonts/noto-cjk/tree/main/Sans), licensed under
the SIL Open Font License 1.1 (`fonts/quota/LICENSE.txt`). Regenerate with
`python tools/generate_quota_font.py` using Pillow 12.3.0; `--check` verifies identical
output and every packed pixel. The 14/16 px LVGL 9 subsets are compiled through
`main/CMakeLists.txt`, selected explicitly by every Chinese label, and checked
against all application source characters (`fonts/quota/inventory.txt`). The
4 bpp PLAIN bitmaps total 44,509 bytes in Flash (19,462 at 14 px and 25,047 at 16 px),
without a runtime font-loading buffer. ASCII and Chinese use these fonts; numeric values
also use built-in Montserrat 20 with an application-owned descriptor and explicit 16 px
fallback for currency symbols and unlimited values. Helper text uses 14 px and metric names
use 16 px. Native previews check the actual linked fonts and label styles at both sizes.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

The offline configuration-backup page bundles
[Project Nayuki QR Code generator v1.8.0](https://github.com/nayuki/QR-Code-generator/releases/tag/v1.8.0)
at `qr/qrcodegen-v1.8.0-es6.js`, unchanged under its embedded MIT license.
SHA256: `6a1116192ed1dd67fa1bf31e77f5817103d71c23bbac24c382e698b7668bdd01`.
It is served locally; no external QR service receives configuration data.
