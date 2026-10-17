[简体中文](THIRD_PARTY.zh_CN.md) · **English**

The browser-environment simulation in `vendor/sdenv` is from pysunday/sdenv,
version 1.1.3: https://github.com/pysunday/sdenv

Its BSD-3-Clause license is retained in `vendor/sdenv/LICENSE`.

Local change: `build/Release/documentAll.js` supplies V8's undetectable object
semantics using the Node runtime instead of compiling the original C++ addon.
The included environment code interprets the site's JavaScript; it does not
launch Chrome, Chromium, Playwright, or any other browser executable.

Other dependencies are pinned in `package.json` and their licenses are included
in their npm distributions. HTTP calls and TLS certificate verification are
handled by the Python application.
