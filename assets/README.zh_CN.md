<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

订阅详情应用使用 `fonts/quota/NotoSansCJKsc-Regular.otf`，来源为
[Noto CJK](https://github.com/notofonts/noto-cjk/tree/main/Sans)，按 SIL Open
Font License 1.1 分发（`fonts/quota/LICENSE.txt`）。使用 Pillow 12.3.0 运行
`python tools/generate_quota_font.py` 可重新生成；`--check` 校验输出一致性和逐像素打包结果。
14／16 px LVGL 9 子集由 `main/CMakeLists.txt` 编译，每个中文标签显式选用，字符覆盖检查扫描全部应用源码，
清单保存在 `fonts/quota/inventory.txt`。4 bpp PLAIN 位图共占 Flash 44,532 字节（14 px 为 19,469，16 px 为
25,063），不需要运行时字库加载缓冲区。ASCII 与中文使用这些字库，数值另用内置 Montserrat 20／28，
通过应用持有的字体描述符显式回退到 16 px，显示货币符号和不限额。说明文字及数据项名称
统一使用 14 px；所有主指标数值使用 28 px，超宽时选用较小字号。本机预览检查实际链接的字体和标签样式。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

离线配置备份页打包了
[Project Nayuki QR Code generator v1.8.0](https://github.com/nayuki/QR-Code-generator/releases/tag/v1.8.0)，
路径为 `qr/qrcodegen-v1.8.0-es6.js`，未修改，保留文件内 MIT 许可。
SHA256：`6a1116192ed1dd67fa1bf31e77f5817103d71c23bbac24c382e698b7668bdd01`。
脚本在设备本地提供，不会将配置传给外部二维码服务。
