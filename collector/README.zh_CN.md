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

## 后台运行与恢复

Windows 在本目录执行 `powershell -File .\service.ps1 install`，安装当前用户的计划任务：
登录后启动、后台无控制台运行、监督进程意外退出后自动重启。把 `install` 换成 `status`、
`start`、`stop`、`remove` 可管理任务。安装前停止已有前台采集器。仅在该用户登录期间运行，
Windows 休眠也会暂停采集。

`start.cmd` 和计划任务使用 `supervise.py`。每轮默认最多 180 秒
（`cycle_timeout_seconds`，允许 30..3600），卡住后终止该轮并重试；默认每 300 秒采集。
`state/collector.log` 每 1 MiB 轮转，保留三份备份。下一轮会重新加载配置。
手动检查仍使用 `collect.py run --once`，先停止后台采集。

凭据 JSON 损坏时状态显示 `secrets_unreadable`，保留最后有效快照，监督进程会重试。
插件摘要通过 schema 2 协议校验后才替换缓存；非法记录转为单账号错误。
同机同平台的重复账号身份合并，优先保留成功数据。COS 临时 Token 与其他凭据一样支持
环境变量优先、文件回退。

TokenHub 下载的脚本在无网络能力的 Windows AppContainer，或 Linux bubblewrap 文件系统／网络
隔离空间中运行，并叠加 Node 权限限制。只允许读取运行时代码，账号文件和采集状态位于边界之外。
隔离不可用时不会退回不受限运行。Linux 需要 `bubblewrap` 和可用的非特权用户命名空间，见 Linux 说明。
