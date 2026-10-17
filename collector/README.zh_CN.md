**简体中文** · [English](README.md)

# 模块化采集器

ChatGPT、TokenHub、ClinePass、DeepSeek、TeleAgent 是独立插件。从
[`config.example.json`](config.example.json) 和
[配置与协议说明](../docs/development/subscription-viewer.zh_CN.md) 开始。

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt
.venv/Scripts/python collect.py init --providers chatgpt
.venv/Scripts/python collect.py run --once
```

在本机修改被忽略的 `config.local.json` 和 `secrets.local.json`，双击 `start.cmd`
持续采集，`configure.cmd` 交互选择模块和设置密钥。每台电脑会生成独立 ID，上传各自的摘要和完整明细；上传前需配置 COS。

构建固件时自动内置本地 COS 配置，平台 Key 留在采集电脑。有单独只读身份时可通过
`device_cos` 使用。不要分享本机凭据、账号数据或私有固件。

Linux 不使用 Docker 时，见[原生部署包说明](linux/README.zh_CN.md)。
