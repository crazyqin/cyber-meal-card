**简体中文** · [English](README.md)

# 赛博饭卡文档导航

[项目介绍](../README.zh_CN.md) · [采集器](../collector/README.zh_CN.md) ·
[Linux 部署](../collector/linux/README.zh_CN.md)

当前 `main` 分支维护赛博饭卡：电脑采集器上传腾讯 COS 私有快照，ESP32-C3 展示
AI 额度、余额和积分。支持的平台和入门步骤见项目介绍。

## 应用使用

| 文档 | 内容 |
| --- | --- |
| [应用与数据协议](development/subscription-viewer.zh_CN.md) | 显示布局、平台配置、账号身份、多电脑同步、私有配置和设备操作 |
| [采集器](../collector/README.zh_CN.md) | 模块化采集和 Windows 启动 |
| [Linux 部署](../collector/linux/README.zh_CN.md) | 直接安装、账号登录、进程管理和升级 |
| [TokenHub 集成](development/tokenhub-research.zh_CN.md) | API 行为、认证和运行时集成 |

## 开发与验证

| 文档 | 内容 |
| --- | --- |
| [Agent 规范](../AGENTS.zh_CN.md) | 仓库规则和按任务阅读的入口 |
| [开发文档索引](development/README.zh_CN.md) | 工程、CI 和发布说明 |
| [AI 开发指南](development/ai-guide.zh_CN.md) | 应用与 BSP 分工、生命周期和验证流程 |
| [环境准备](development/engineering/environment-setup.zh_CN.md) | ESP-IDF 5.5.3 和主机工具 |
| [构建与测试](development/engineering/build-and-test.zh_CN.md) | 仓库检查、主机测试、固件构建和产物校验 |
| [固件布局](development/engineering/firmware-layout.zh_CN.md) | 分区、合并镜像、烧录偏移和设备配置 |
| [必需技能](../skills/README.zh_CN.md) | 开发、环境配置、构建、真机测试和调试 |

运行 `bash tools/validate.sh --static` 进行仓库检查和离线主机测试；激活 ESP-IDF
环境后运行 `bash tools/validate.sh` 进行完整验证。真机检查与构建、主机测试分别报告。

## 硬件与可复用平台参考

目标硬件为 FoloToy AI Passport：**ESP32-C3、8 MB Flash、无 PSRAM、240 × 320
屏幕、三个实体按键**。硬件支持位于 `components/bsp`，赛博饭卡应用位于 `main`，
电脑采集器位于 `collector`。

| 参考文档 | 内容 |
| --- | --- |
| [硬件索引](hardware-design/README.zh_CN.md) | 板级接口和硬件文档 |
| [硬件开发指南](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md) | 引脚、总线、显示、音频、电池和验收检查 |
| [中文字体](development/engineering/lvgl-chinese-fonts.zh_CN.md) | 字形覆盖、控件字体和渲染检查 |
| [Wi-Fi 配网参考](development/engineering/wifi-provisioning.zh_CN.md) | 上游蓝牙配网示例；本应用的热点设置见应用说明 |
| [社区作品](reference/README.zh_CN.md) | 其他平台应用和可复用开发经验 |
| [源素材与许可证](../assets/README.zh_CN.md) | 字体、二维码组件和各自许可证 |
| [品牌参考](brand/README.zh_CN.md) | 上游平台视觉素材 |

## 贡献与项目历史

[贡献指南](contribution/README.zh_CN.md) ·
[提交规范](contribution/commit-and-pr.zh_CN.md) ·
[Fork 指南](fork-guide.zh_CN.md) · [上游变更记录](CHANGELOG.zh_CN.md)

[获取帮助](../.github/SUPPORT.zh_CN.md) · [安全说明](../.github/SECURITY.zh_CN.md) ·
[行为准则](../.github/CODE_OF_CONDUCT.zh_CN.md) · [MIT 许可证](../LICENSE)
