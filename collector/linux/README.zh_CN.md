**简体中文** · [English](README.md)

# Linux 采集器部署

这个包直接在 Linux 运行，不用 Docker。支持 x86_64、ARM64，要求 glibc 2.28 或更新版本，
例如 Ubuntu 20.04+、Debian 11+。首次安装需要 Bash、curl、tar、coreutils、系统 CA 证书和网络。
安装脚本下载固定版本的 Python 3.12、Node 24、Codex CLI，先校验 SHA-256，再安装本目录内的
Python/npm 依赖。不要求事先安装这些运行时，不修改系统软件包。暂不支持 Alpine/musl 或 32 位系统。

## 部署

把压缩包解压到准备长期保留、当前用户有写权限的目录，然后执行：

```bash
tar -xzf ai-collector-linux-private-20261017-teleagent.tar.gz
cd ai-collector-linux
bash install.sh
bash collector.sh login
bash collector.sh check
bash collector.sh start
bash collector.sh status
```

`login` 会显示设备授权网址和验证码，用浏览器登录你希望采集的 ChatGPT 账号。账号未开启设备
授权时，按[官方认证说明](https://learn.chatgpt.com/docs/auth)开启相应功能。登录保存在本部署目录
的 `accounts/`，与已有 Codex 安装分开。包内不带当前电脑的 ChatGPT／TokenHub／TeleAgent 登录或平台 Key。

**私有包**已在 `settings/cos.local.json` 预填当前 COS 桶、地域、前缀和凭据，请勿公开分发。
安装会初始化 `config.local.json` 与 `secrets.local.json`，不打印密钥。重复安装保留已有配置。
通用公开包不含 COS 凭据，需要执行 `bash collector.sh configure` 填写。

默认启用 ChatGPT，其他模块已带齐但关闭。新电脑首次采集会自动生成独立采集器 ID，不会覆盖原
电脑的对象，固件会自动发现。不要复制正在运行的部署目录中的 `state/` 或登录目录。

## 其他平台和账号

```bash
bash collector.sh configure          # 选择模块，输入本机密钥
bash collector.sh tokenhub-auth      # 输入 TokenHub Satoken，不回显
bash collector.sh teleagent-auth     # 输入星辰门户 operationToken，不回显
bash collector.sh restart            # 应用配置修改
```

TokenHub 先在 `configure` 中启用，再执行 `tokenhub-auth` 保存 Satoken。安装器会装好本地
Node/WAF 依赖；会话失效后需要更新 Satoken。ClinePass、DeepSeek 的 Key 只在采集电脑上输入。
同一账号在多电脑使用不同 Key 时，显式填写相同 `account_id` 即可合并。

TeleAgent（星辰积分门户）先在 `https://agent.teleai.com.cn/` 登录，通过浏览器开发者工具复制
经过 URL 解码的 `operationToken` Cookie。用 `teleagent-auth` 私下保存，在 `configure` 启用
`teleagent-main`，再重启。固件显示积分余额、今日消耗、今日新增，采集器还保留积分分组和有效期、
任务数及对话数到私有明细。会话可能过期，出现 `auth` 错误时更新凭据。同一账号多机显式使用
相同 `account_id`。不要把登录凭据贴到聊天或写入固件。

升级现有部署时，先停止采集，把新包解压到另一个同级目录，只把其中的 `app/` 和 `deploy.py`
复制覆盖到原部署目录。保留原 `.runtime/`、`.venv/`、`config.local.json`、`secrets.local.json`、
`settings/`、`accounts/`、`state/`，新模块无需重装运行时。若旧配置还没有星辰行，先添加再保存凭据：

```bash
bash collector.sh cli add teleagent --id teleagent-main --name teleagent --account-id teleagent-personal --auth-file settings/teleagent-auth.local.json
bash collector.sh teleagent-auth
bash collector.sh check
bash collector.sh start
```

新增行默认启用。不要用新包预设覆盖已有设置，也不要在电脑间复制机器状态。

增加另一个 ChatGPT 登录：

```bash
bash collector.sh cli add chatgpt --id work --name work --codex-home accounts/work
```

然后执行 `bash collector.sh login work` 并重启采集器，会自动找到内置 Codex。显示别名使用可打印 ASCII 字符。
账号配置中的路径相对于部署目录下的 `config.local.json`；不同登录使用不同目录。

## 管理和排查

```bash
bash collector.sh start       # 后台运行，关闭 SSH 后仍采集
bash collector.sh stop        # 只停止经过身份核对的本目录采集进程组
bash collector.sh restart
bash collector.sh status      # 查看进程及最近采集、上传状态
bash collector.sh logs        # 最近 100 行日志
bash collector.sh check       # 停止后台采集后，单次实采并上传
bash collector.sh run         # 前台运行，可交给 systemd 等进程管理器
bash collector.sh runtime-check
```

每五分钟采集一次，输出 `accounts=... errors=0 upload=ok` 表示本轮成功。
`state/status.json`、`latest.json`、`details.json` 保存本地结果，日志在 `state/collector.log`。
失败不会刷新旧数据的源采集时间。`check` 与后台进程共用状态锁，手动单次检查前先停止后台进程。
`runtime-check` 只检查本地依赖，不代表账号认证和真实接口可用。编辑模块配置后需要重启采集器。

`start` 使用监督进程，失败后重试，每轮默认最多 180 秒，日志每 1 MiB 轮转，保留三份备份。
如需调整，在本地配置设置 `cycle_timeout_seconds`（30..3600）。下一轮重新加载配置，
凭据文件异常时保留最后成功的数据。

需要自动启动时执行 `bash collector.sh service-install`，为本部署安装并启动 systemd 用户服务，
支持失败重启和进程组清理。此后普通 `start`、`stop`、`restart`、`status` 会管理该服务。
若需要登录前启动、退出登录后继续运行，管理员可能需要执行
`sudo loginctl enable-linger "$USER"`。没有 systemd 时仍可用后台 `start`，但机器重启后不会自动恢复。

TokenHub 需要操作系统隔离：安装 `bubblewrap`（Ubuntu/Debian：
`sudo apt-get install bubblewrap`），并确保主机安全策略允许非特权用户命名空间。
不使用 Docker。下载的脚本不能挂载账号／状态目录，也不能使用宿主网络。
隔离不可用时不会退回不受限运行，该平台会报采集错误，其他平台仍可继续；Windows 使用 AppContainer。

首次安装要从 GitHub、nodejs.org 下载运行时，从 PyPI/npm 下载依赖，并非离线运行时镜像。
curl 支持下载代理环境变量。实际采集仍需要访问所选平台及 COS，必要时填写平台／COS 的代理字段。
下载失败可重跑安装，已校验的归档会复用。若已存在其他版本运行时，安装器会保留并提示换新目录。

生成的凭据使用私有文件权限。不需要 root 登录、Docker 服务、系统级 Python/Node 或固件平台 Key。
`manifest.json` 记录部署文件及哈希，`runtime-lock.json` 记录各架构的下载地址和校验值。
部署包已通过归档校验、Shell 语法和主机逻辑测试，真实 Linux 安装仍需单独验证。
