**简体中文** · [English](tokenhub-research.md)

# 智云 TokenHub 接入研究

2026-10-15，最初在已登录 Chrome 中研究[费用中心](https://token.telecomjs.com/finance/cost)。
后续 `D:/aipassport` 的纯 HTTP 实现已成功计算动态校验、发送封装请求并解密业务 JSON，
运行时不启动 Chrome。用户现已接受电脑／服务器采集、COS 分发到设备的架构，并要求
保留现有 ClinePass、DeepSeek、Codex 功能。

## 已确认的接口

官网前端使用 Axios，基础路径为 `/api`，登录会话放在 `Satoken` 请求头。
费用卡片使用以下只读查询：

```http
POST https://token.telecomjs.com/api/heimdall-product/cost-center/customer/card-with-time-range
Content-Type: application/json
Satoken: <现有登录会话>
```

浏览器防护处理前，前端的业务请求体为：

```json
{"adminView": false, "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD"}
```

费用中心默认选择当天。已解码的卡片响应包含以下数值字段，各字段不能混用：

| 字段 | 页面或客户端中观察到的含义 |
| --- | --- |
| `allocateAmount` | 人民币分配总额，页面将 null 显示为不限额 |
| `totalConsumption` | 累计消费 |
| `monthConsumption` | 人民币本月累计消费 |
| `monthRecharge` | 本月充值 |
| `balance`、`platformGiftBalance`、`totalBalance` | 钱包余额字段；分配额度型账号可能为零 |
| `timeRangeConsumption` | 所选日期范围内的消费 |

当前账号角色的页面显示分配总额与本月累计消费。不能把零值钱包字段当作该账号的剩余
分配额度；也不能把“分配总额减本月消费”直接标为权威余额，因为尚未确认分配周期及重置规则。

公开客户端存在 `refreshToken` 函数，但实现为空，目前没有确认可无人值守的会话续期流程，
不推测会话有效期。官方模型文档中的推理 API Key 与网页 `Satoken` 会话不同，尚未验证
推理密钥能否查询账务。

## 普通客户端直连结果

浏览器网络层观察到的请求与响应经过防护处理，并非前端原始 JSON；网页自身能解码并显示
卡片。使用有效 `Satoken`、JSON 请求体和应用 User-Agent 发起两次普通 HTTPS POST，
均收到：

```text
HTTP 412
Content-Type: text/html; charset=utf-8
返回浏览器校验页面，而非 JSON
```

第二次同时补充了观察到的常规 Referer/Origin 与灰度路由请求头。两次均未复制动态防护
请求头或校验 Cookie。结果说明已测试的普通客户端直连路径失败，不代表所有官方接入路径
均不可用。

[官方文档](https://token.telecomjs.com/document) 列出 `aigw.telecomjs.com` 的模型推理接口。
搜索“余额”只找到模型错误说明，未找到公开余额查询 API。研究未调用模型推理、付款、
创建密钥或修改账号设置。此记录不含凭据与真实账号金额；Satoken 不嵌入固件。
原私有构建仍包含其已有 COS 凭据，不应公开分享固件或调试文件。

## 已实现的部署

Python/curl_cffi 负责所有网络请求。本地 Node.js 环境执行下载的防护代码，计算 Cookie
与请求封装，并在同一 JavaScript 会话中解密响应。真实业务接口已返回 `returncode: 0`。
Node 不进行网络连接，Chrome 不是运行依赖；该环境不嵌入 ESP32-C3。

采集器将精简报告上传到现有私有 COS 前缀下的 `tokenhub.json`，设备使用已有 COS 配置
签名 GET。报告不包含网页登录令牌或原始接口响应。固件校验结构、币种、金额字符串和
源时间，显示估算剩余额度、本月消费与今日消费。失败保留最近有效金额，成功下载也不会
重置旧数据的采集时间。操作及数据约定见[应用说明](subscription-viewer.zh_CN.md)。

上面的 HTTP 412 结果仅描述未封装的普通请求；现已通过动态协议处理完成 HTTP 采集。
Satoken 续期与服务商的分配额度重置语义仍未验证。
