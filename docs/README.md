**English** · [简体中文](README.zh_CN.md)

# Cyber Meal Card documentation

[Project overview](../README.md) · [Collector](../collector/README.md) ·
[Linux deployment](../collector/linux/README.md)

The current `main` branch contains Cyber Meal Card: computer collectors publish
private Tencent COS snapshots and the ESP32-C3 displays AI quotas, balances and
points. Start with the project overview for supported services and setup.

## Application

| Guide | Contents |
| --- | --- |
| [Application and data protocol](development/subscription-viewer.md) | Dashboard layout, provider setup, account identities, multi-computer synchronization, private configuration and device controls |
| [Collector](../collector/README.md) | Modular collectors and Windows startup |
| [Linux deployment](../collector/linux/README.md) | Direct installation, account login, process management and upgrades |
| [TokenHub integration](development/tokenhub-research.md) | API behavior, authentication and runtime integration |

## Development and verification

| Guide | Contents |
| --- | --- |
| [Agent instructions](../AGENTS.md) | Repository rules and task-specific reading |
| [Development index](development/README.md) | Engineering, CI and release guides |
| [AI development guide](development/ai-guide.md) | Application/BSP boundaries, lifecycle and validation workflow |
| [Environment setup](development/engineering/environment-setup.md) | ESP-IDF 5.5.3 and host tooling |
| [Build and test](development/engineering/build-and-test.md) | Static checks, host tests, firmware build and verified artifacts |
| [Firmware layout](development/engineering/firmware-layout.md) | Partitions, merged images, flashing offsets and stored configuration |
| [Required skills](../skills/README.md) | Development, setup, build, device testing and debugging |

Run `bash tools/validate.sh --static` for repository checks and offline host tests.
Run `bash tools/validate.sh` in the activated ESP-IDF environment for the full gate.
Report device checks separately from builds and host tests.

## Hardware and reusable platform references

The target is FoloToy AI Passport: **ESP32-C3, 8 MB Flash, no PSRAM, a 240 × 320
screen and three physical buttons**. Hardware support lives in `components/bsp`;
Cyber Meal Card application code lives in `main` and collectors in `collector`.

| Reference | Contents |
| --- | --- |
| [Hardware index](hardware-design/README.md) | Board interfaces and hardware documentation |
| [Hardware development guide](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) | Pins, buses, display, audio, battery and acceptance checks |
| [Chinese fonts](development/engineering/lvgl-chinese-fonts.md) | Glyph coverage, widget fonts and rendering checks |
| [Wi-Fi provisioning reference](development/engineering/wifi-provisioning.md) | Upstream Bluetooth provisioning example; this application's hotspot setup is documented in the application guide |
| [Community projects](reference/README.md) | Other platform applications and reusable development experience |
| [Source assets and licenses](../assets/README.md) | Fonts, QR components and their licenses |
| [Brand references](brand/README.md) | Upstream platform visuals |

## Contribution and project history

[Contribution guides](contribution/README.md) ·
[Commit conventions](contribution/commit-and-pr.md) ·
[Fork guide](fork-guide.md) · [Upstream changelog](CHANGELOG.md)

[Get help](../.github/SUPPORT.md) · [Security](../.github/SECURITY.md) ·
[Code of conduct](../.github/CODE_OF_CONDUCT.md) · [MIT license](../LICENSE)
