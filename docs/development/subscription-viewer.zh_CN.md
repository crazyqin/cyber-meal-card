**简体中文** · [English](subscription-viewer.md)

# AI 订阅详情与模块化采集器

所有平台都由电脑采集。ESP32-C3 只读取腾讯 COS 的私有快照，不再调用 ClinePass、DeepSeek、
TokenHub、TeleAgent 或 OpenAI 的 API，也不需要平台 Key。独立应用入口是 `main/quota_app.c`，按服务和账号
展示余量、源采集时间和重置倒计时，不使用硬件测试菜单作为启动界面。

## 显示布局

所有账号统一使用简洁的 240 × 320 布局：顶部显示连接状态和电量，下方为一处服务名、
三块数据区域，底部显示采集状态、数据距今时间和页码。所有服务都将第一项主数值
直接放在深海蓝背景上，与服务标题左对齐，不再包裹卡片。可选说明（如 TokenHub 的分配说明）
放在主数值下方，下方两张紧凑明细卡在各服务间保持相同位置，ClinePass 和 ChatGPT 也不例外。
额度重置倒计时保留在对应数值下面；双币种保留各自余额和构成，第三项显示采集到的 DeepSeek
API 可用状态。缺少指标时在原位置用弱化的 `--` 表示，不改变布局，也不补成零。
账号／套餐信息以小字放在服务名右侧，标题与主指标标签之间保留 14 px 空隙。“剩余额度”“余额明细”等
重复副标题、重复服务名以及技术性套餐文案不再显示，自定义账号别名保留在标题旁的小字区域。
ChatGPT 例外，套餐合并进标题，显示为 `ChatGPT Plus` 或 `ChatGPT Team`：`plus` 显示为 `Plus`，
`team`／`business` 显示为 `Team`。
其他套餐保留原值，缺失时留空，不根据账号别名猜测套餐。
标签保持静止，过长别名用省略号显示，避免
持续滚动。

| 服务 | 数据 |
| --- | --- |
| ClinePass | 5 小时、每周、每月剩余百分比及重置倒计时 |
| ChatGPT | 各窗口剩余百分比，以及可用重置次数、可用机会中已知最近到期的一次 |
| DeepSeek | 账户、充值、赠送余额；双币种账号显示两种余额、各自构成及 API 可用状态 |
| TokenHub | 本月估算剩余额度、本月消费、今日消费；不限额状态明确展示 |
| TeleAgent（星辰） | 全部调用端、全部模型的积分余额、今日消耗积分、今日新增积分 |

深海蓝主题使用深蓝背景（`#0B1421`）、蓝色卡片（`#142338`）、浅色正文（`#EFF5FF`）、
蓝色强调（`#78B7FF`）和暖橙提醒（`#FFBF80`）。积分单位由主指标“积分余额”表达，
TeleAgent 标题旁不再单独显示“积分”，数值不使用货币符号。
各平台标题采用独立强调色：ChatGPT 薄荷绿（`#4FD1A5`）、ClinePass 暖金（`#F5C16C`）、
DeepSeek 蓝紫（`#7E9BFF`）、TeleAgent 青蓝（`#58C8E8`）、TokenHub 紫色（`#C29CFF`）。
通用平台和设置页使用标准浅色标题。主数值使用 Montserrat 36，长数值按需缩小到 28、20
及现有中文字体。明细卡增加细蓝色边框和圆角，额度进度条沿用平台标题色，低余量仍使用提醒色。
各平台明细卡的位置保持一致。电池图标按电量填充，20% 及以下使用暖色提醒，读数不可用时显示
空轮廓和 `--`。页码置于小胶囊内，设置及空状态引导使用内嵌面板和 14 px 中文字体，字库子集已覆盖设备设置文案。

金额统一用人民币或美元符号及两位小数；正数不足一分时明确标记小于一分，极大金额使用明确的
科学计数法。TokenHub 的分配金额和估算说明只放在对应剩余额度旁，未知数据保持未知。低余量、
超额和数据过期／错误状态使用暖色提醒。距今时间按原始采集时间计算，不能用设备下载时间刷新。
采集时间缺失或在未来时不显示为刚采集；超过七天显示北京时间（UTC+8）的原始日期。
同步进度放在顶部，不覆盖底部的采集异常。ChatGPT 对最近到期的一笔重置显示倒计时，
不足 24 小时提醒；到期后提示等待采集确认，设备不会自行扣减采集到的次数。
设置页与空状态沿用相同的头部、底部，账号页短按操作不变。手动切换账号或唤醒暗屏后，自动轮播暂停
30 秒，其余时间每 10 秒轮播；设置页面暂停轮播。

展示逻辑独立放在 `main/quota_presentation.c`，实际控件位于 `main/quota_ui.c`。本机屏幕检查使用
相同控件和字体渲染：

```sh
cmake -S tools/quota_preview -B build/quota-preview -DCMAKE_BUILD_TYPE=Debug
cmake --build build/quota-preview
mkdir -p build/ui-preview
build/quota-preview/quota_preview build/ui-preview
```

合成数据的 PPM 预览覆盖各服务、双币种、不限额、重置信息缺失、长金额、过期／离线／同步中、
设置页和空状态，检查实际字体覆盖、标签边界、静态长文本和重复更新。32 位本机构建使用设备的
24 KiB LVGL 内存池；64 位构建因指针更大使用 40 KiB。本机渲染与内存数字均不代表真机验证。

## 启动采集器

维护中的统一采集器位于 [`collector/`](../../collector/collect.py)。在该目录运行：

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt
.venv/Scripts/python collect.py init --key-file ../../key.txt --providers chatgpt,clinepass,deepseek,tokenhub --tokenhub-auth ../../tokenhub_collector/.http-auth.json
.venv/Scripts/python collect.py doctor
.venv/Scripts/python collect.py run --once
.venv/Scripts/python collect.py run
```

`key.txt` 导入是可选的，兼容当前本地 ClinePass、DeepSeek、COS 文本格式。
`config.local.json` 保存模块选择和密钥引用，`secrets.local.json` 单独保存凭据，两者均被 Git
忽略。同名环境变量优先于文件中的凭据。控制台不打印密钥或任意第三方异常原文。JSON 中的路径
相对于配置文件目录，`~` 表示当前用户目录。双击 `start.cmd` 持续采集，`check.cmd` 单次采集。
每轮重新读取凭据；修改模块配置后需重启进程。

可用 [`config.example.json`](../../collector/config.example.json) 作为可移植起点。双击 `configure.cmd` 可交互选择模块、修改 COS 和密钥，密钥输入不回显。无需改代码即可操作：

```powershell
python collect.py list
python collect.py disable tokenhub-main
python collect.py enable tokenhub-main
python collect.py add chatgpt --id work --name work --codex-home C:/accounts/work/.codex
```

只加载启用的模块。ChatGPT 使用本机已有的 Codex ChatGPT 登录及可执行程序，可通过 `codex_bin`
指定程序。多个账号分别配置 `codex_home`；采集器不登出或切换活动登录，而是启动独立 app-server
子进程，只调用初始化与账号读取方法。可选 token 统计失败不会丢弃有效额度。仅 API Key 登录的
Codex 无法提供 ChatGPT 订阅额度。

TokenHub 复用既有 HTTPS/WAF 协议，不启动浏览器。启用时需要 Node.js 24，并在
`collector/tokenhub_runtime/waf_runtime` 执行 `npm ci` 安装依赖。`TOKENHUB_NODE` 可指定 Node
绝对路径，也可按账号设置 `node_bin`。初始化会把提供的认证文件复制到忽略的本地文件，并记录
找到的 Node 路径。本地 `auth_file` 提供 Satoken，会话过期后需要更新。Windows 保留 curl_cffi 所需的
显式系统编码证书路径。ClinePass、DeepSeek 只向固定 HTTPS API 域名发送凭据，不跟随重定向。

TeleAgent 读取星辰门户 `https://agent.teleai.com.cn/meter/super-agent/user/portal` 下的
`/points/detail` 和 `/usage/summary` GET 接口。今日日期按北京时间计算，不限制调用端或模型。
将已登录门户的 `operationToken` Cookie 做 URL 解码，以 `{"operation_token":"你的会话"}`
保存到忽略的 `teleagent-auth.local.json`，再添加模块：

```powershell
python collect.py add teleagent --id teleagent-main --name teleagent --account-id teleagent-personal --auth-file teleagent-auth.local.json
```

若配置中已有该行，启用即可；修改配置后重启。凭据只在采集电脑上保存，会话过期需更新。
两次读取均成功、三项指标都存在后才替换成功快照，缺失指标不会变成零。积分分组及有效期、
任务数、对话数保留在 `details.json`；门户提示用量明细约有 30 分钟延迟。采集不执行签到、
领取奖励或消费操作。同一账号在多电脑应显式使用相同 `account_id`，不同账号必须区分；
轮换登录令牌不作为账号身份。

## 多电脑和账号身份

各电脑分别发布 `<prefix>/<collector_id>/latest.json` 与 `details.json`，不写共享索引，也不改写
另一台电脑的对象。`collector_id: auto` 将随机 ID 与本机指纹持久化；复制配置甚至状态目录到
另一台电脑时，会自动生成新 ID。手动指定的 ID 必须唯一。本地文件锁防止两个进程占用同一状态
目录；独立进程不能共用同一个手动 ID。

固件通过 COS ListBucket 分页发现目录。按平台与稳定 `account_id` 识别账号，多机取**源采集
时间**最新的记录，时间相同优先成功记录，不依赖上传顺序。同名不同账号独立显示；列表重排或
获胜的采集电脑变化时，仍按稳定身份保持选择。ChatGPT 从账号／工作区身份生成哈希；API 平台从
Key 生成哈希。同一账号使用不同 API Key 时，可显式填写相同 `account_id`。TokenHub 多机应填写
相同的 `account_id`，或使用相同的本地账号 `id`。不同人的账号不得复用该身份。

有效报告只整体替换其自身的账号。删除或禁用账号后，下次发布即可移除。HTTP 404 或完整目录
扫描会移除已不存在的采集器；停止运行但保留对象的电脑仍显示旧数据，并标记过期。采集失败保留
旧源时间与数值，带上错误状态；更换凭据或账号配置后失效本地旧缓存。上传失败保留成功的本地
文件，下轮重试。设备仍可选用另一台电脑较新的观察结果。

## 数据协议和扩展

`schema_version: 2` 包含 `collector_id`、UTC `collected_at` 与 `accounts`。每条记录包括
`local_id`、`provider`、稳定 `account_id`、可打印 ASCII 的 `name` 与 `label`、`status`、
`checked_at` 和可空的源 `collected_at`。展示字段有 `plan`、最多三个 `windows`、最多两种
`balances`、可选 `tokenhub` 金额、`reset_credits` 或 `points`。窗口百分比、持续时间和重置时间戳经过校验。
金额始终使用十进制字符串，不合并币种。TokenHub 剩余是分配减本月消费的估算，支持不限额和
负剩余。失败不会伪造新源时间或零使用量。

`details.json` 保存启用模块返回的全部业务字段，包括未识别字段、全部额度分组、credits 和
使用统计；递归移除认证／个人身份字段及配置中的密钥值。这仍是私有账号使用数据。
固件只下载小于 24 KiB 的 `latest.json`，明细上限 2 MB。先发布明细，再发布摘要；二者各自原子
更新，关联时必须核对 `collected_at`。上传部分失败可能导致不同代的数据，摘要仍可独立使用。

新增 `collector/plugins/<provider>.py`，导出 `LABEL` 和 `collect(config, secrets) -> dict` 即可。
返回稳定 `identity`、可选标准展示字段，以及包含完整业务数据的 `details`。未知返回字段保留在
明细 `data` 中。已知错误使用 `CollectionError(code)` 返回可安全显示的代码。通用额度窗口、余额和积分
无需改固件即可展示；特定平台的专用页面可后续独立增加。没有返回的指标保持未知。允许空报告。
设备每轮支持 32 条来源记录、32 个采集目录；多机重复记录也计入来源容量。超限时明确提示，不会
静默丢弃原有采集器的记录。

兼容旧 schema-1 Codex 目录报告。不再轮询根目录旧 `tokenhub.json`，请启用新的 TokenHub 模块。
NVS 配置版本 3 的 Wi-Fi 和 COS 保持兼容，旧平台 Key 字段仅供备份兼容，不参与采集。快照缓存
版本 7 丢弃旧布局，开机缓存始终标记过期。

`points` 为独立展示类型，包含 `balance`、`spent_today`、`added_today` 三个非负十进制字符串，
每个最多 23 个字符，不可与额度窗口、余额、TokenHub 金额或重置机会混用。设备复用原余额存储，
快照 RAM 占用不增加，后续积分平台只需新增采集模块，无需修改固件。

## ChatGPT 重置机会与 credits

模块调用官方 `account/rateLimits/read`，开启重置明细，并可选调用 `account/usage/read`。
保留全部额度分组、工作区 credits、重置机会列表、`availableCount`、获得／到期时间、token 汇总
和每日统计。接口依据见[官方 app-server 协议](https://learn.chatgpt.com/docs/app-server)。

可用重置机会**不是付费充值次数**。该协议没有返回购买历史，采集器不会编造该次数。
`availableCount` 是权威总数，详情列表可能被服务端截断；缺失次数为 null，详情 null 和空数组
含义不同。`latest_expires_at_unix` 表示最近获得、当前可用的重置机会的到期时间；
`next_expires_at_unix` 表示已知的最近到期时间，两者都不是最近一次兑换记录。原始明细保留，
便于未来固件决定展示。采集不会调用消费重置、付费、模型推理、发邮件或其他修改方法。

当前 ChatGPT 页面显示主要额度窗口和可用重置次数，第三行使用 `next_expires_at_unix` 显示可用
机会中已知最近到期的一次，采用北京时间。次数为零时显示暂无可用重置；最近到期时间未知时明确
显示未提供，不会退回显示最近获得机会的到期时间。不论固件当前展示哪些字段，采集器都保留上述
扩展信息。

## 固件配置与操作

存在 `collector/config.local.json` 时，CMake 自动根据其中 COS 参数和凭据引用生成忽略文件
`build/private/quota_private_cos.h`，不用先手动生成头文件。可选 `device_cos` 覆盖允许为设备
指定单独的只读身份。配置错误会使构建失败，且不打印值。内置 COS 凭据和可选 Wi-Fi 配置，平台 Key 和 ChatGPT
登录令牌不会进入固件。设备保存的 COS 不完整时整体使用内置值；完整自定义配置优先保留。
若要更换设备内已有完整配置，使用热点配置页。含 COS 凭据的固件与 ELF 应保持私有。

没有本地采集器配置时，仍支持已有私有头文件。也可手动刷新：

```powershell
python tools/prepare_private_cos.py --config collector/config.local.json
```

工作区上级目录存在 `key.txt` 时，CMake 还会读取其中的 `ssid` 与 `pass`（或 `password`）。
只从该文件导入这两个 Wi-Fi 字段，忽略平台 Key。SSID 按 UTF-8 限制为 32 字节；密码允许为空
（开放网络）、8～63 字节，或 64 位十六进制 PSK。不完整或无效的 Wi-Fi 字段会使构建失败，
不会打印内容。固定宽度 C 转义保证中文 SSID、引号和反斜杠字节一致。可用 CMake 缓存选项
`QUOTA_PRIVATE_KEY_FILE` 指定其他本地文件；文件变化会触发重新生成。没有 Wi-Fi 字段时维持
原 COS 内置行为。

启动时没有已保存 Wi-Fi 的设备会自动采用内置 SSID／密码并联网；已有 Wi-Fi 优先，长按确定，
选择「Wi-Fi 设置 → 手机配网」可修改。默认值在私有应用镜像中，不需要额外文件系统或数据分区。手动生成等价命令：

```powershell
python tools/prepare_private_cos.py --config collector/config.local.json --key-file ../key.txt
```

没有内置 Wi-Fi 时，全新固件需要输入 2.4 GHz Wi-Fi。长按确定，选择「Wi-Fi 设置 → 手机配网」，
连接随机 WPA2 密码热点后用浏览器访问 `http://192.168.4.1`。热点引导页长按确定关闭热点并返回 Wi-Fi 菜单。
配置页仅对热点接口开放。Wi-Fi 修改先测试再保存，失败保留原配置。
网页可覆盖 COS，并通过本地 JSON 或离线多张二维码备份恢复。可选字段留空保留旧值，明确的
清空选项才删除对应配置。

| 按键 | 行为 |
| --- | --- |
| 上／下短按 | 切换上一个／下一个账号 |
| 确定短按 | 未联网时先连接 Wi-Fi，再同步；手动请求至少间隔十秒，提前请求会排队 |
| 确定长按 | 打开设备设置；设置内返回或取消修改 |
| 上键长按 | 开启／关闭十秒轮播 |
| 下键长按 | 暂无功能 |
| 变暗后首次按键 | 唤醒背光 |

设置菜单用上下键选择、确定键进入。Wi-Fi 提供「连接并同步」和「手机配网」；网页显示已保存网络名称，
支持显示／隐藏当前输入的密码，不会向浏览器返回已保存密码。其他设置用上键增加、下键减少，确定键保存，
长按确定取消；保存失败时保留调节页，允许重试。

| 设置 | 范围和行为 | 默认值 |
| --- | --- | --- |
| 音量 | 0–100%，每次 5%；零为静音，调节时播放 80 ms 试听音 | 40% |
| 背光亮度 | 10–100%，每次 5%；立即预览，取消恢复已保存亮度 | 75% |
| 自动调暗 | 常亮或 15／30／45／60／120／300 秒；闲置后降至 5%，首次按键仅唤醒 | 45 秒 |
| 自动轮播 | 开启／关闭，每十秒切页；设置页暂停轮播 | 关闭 |

偏好经校验后保存在独立的 `quota_device/preferences` NVS 记录中，保持原 Wi-Fi／COS 配置结构兼容；
记录缺失或无效时使用上述默认值。现有 JSON／二维码备份仅覆盖网络和 COS 配置，不包含设备偏好。
音频由按需启动的工作任务通过小缓冲区播放，等待声音输出后休眠 codec；写 Flash 前等待试听停止确认。
按键和 LVGL 回调不播放音频、不写 NVS。`main/quota_settings.c` 将导航、调节、校验和调暗规则与
ESP-IDF／LVGL 分离，便于主机测试。

确定键与设置网页唯一的“连接并同步”按钮使用同一操作。已联网时复用当前连接，未联网时
先连接 Wi-Fi，再读取 COS；没有保存的 Wi-Fi 时，确定键打开设置。连接失败保留缓存并提示
网络异常；保存 Wi-Fi 配置后会自动请求同步。

每五分钟自动同步；超过十五分钟、时间在未来或失败时标记过期。倒计时结束不会在设备本地补充
余量。SNTP 校时后串行 HTTPS 读取，校验证书并禁止重定向。设备身份需要该前缀的 ListBucket、
GetObject 权限，采集端需要自身对象的 PutObject 权限。程序不修改桶策略或 ACL；临时 COS
凭据过期后需要更新。

## 验证与交付

激活 ESP-IDF 5.5.3 后运行 `bash tools/validate.sh`，执行仓库检查、主机测试、固件构建、合并布局
校验和匹配调试归档。采集器主机测试完全离线，不用真实凭据。实采和 COS 验证单独报告。
新增中文已进入生成字体清单。目标为 ESP32-C3、8 MB Flash、无 PSRAM、240 × 320 屏幕。

验证后的完整固件从 `0x0` 烧录，合并刷写可能重置 NVS。若已使用本应用匹配分区表，可将应用
单体写到 `0x20000` 保留 NVS；应用单体不得写到 `0x0`。构建和测试命令不会刷机。页面渲染、
热点交互、网络稳定性、内存余量和实体按键需经用户同意后另行真机测试。

## Git 版本管理

应用固件和采集器源码一起维护在 `main`，后续开发使用功能分支。公开历史使用虚构测试数据，
旧本地分支属于私有工作历史，不得一起批量推送。后续修改使用 Conventional Commits
格式提交，每次提交前运行 `bash tools/validate.sh --static`；固件变更还要通过完整验证。
Shell 脚本在所有平台使用 LF 换行，Windows 启动脚本使用 CRLF。

```sh
git status --short --branch
git diff
git add <changed-source-files>
git diff --cached --stat
git diff --cached --check
git commit -m "fix(collector): describe the change"
git log --oneline -5
```

本地配置、API Key、登录会话、账号采集数据、安装依赖、私有固件和部署包均已忽略。
示例配置、依赖锁文件、字体许可证、源素材和测试继续纳入版本管理，不要强制添加私有文件。
本地提交不会自动发布或备份到服务器。`origin` 指向自己的 GitHub 项目，`upstream` 保留原
公开上游地址；审核后的项目更新使用 `git push origin main` 推送，不要使用 `--all` 或
`--mirror` 发布本地历史。
