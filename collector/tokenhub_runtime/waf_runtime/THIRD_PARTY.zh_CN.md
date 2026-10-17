[English](THIRD_PARTY.md) · **简体中文**

# 第三方组件

`vendor/sdenv` 的浏览器环境模拟代码来自 pysunday/sdenv 1.1.3：
https://github.com/pysunday/sdenv 。BSD-3-Clause 许可证保留在 `vendor/sdenv/LICENSE`。

本地修改：`build/Release/documentAll.js` 使用 Node 运行时提供 V8 不可检测对象语义，
无需编译原 C++ 扩展。模拟环境执行站点 JavaScript，不启动浏览器。

其他依赖在 `package.json` 和 `package-lock.json` 固定版本，许可证随 npm 包分发。
HTTPS 请求和 TLS 证书校验由 Python 处理。
