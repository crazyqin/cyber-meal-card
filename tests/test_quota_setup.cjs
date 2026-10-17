/* Optional browser QA: NODE_PATH must expose Playwright. No provider calls. */
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');

(async () => {
  const root = path.resolve(__dirname, '..');
  const browser = await chromium.launch({headless: true, executablePath: process.env.QUOTA_TEST_BROWSER});
  try {
    const page = await browser.newPage({viewport: {width: 390, height: 844}});
    let posted, reject = false;
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('**/*', route => {
      const url = new URL(route.request().url());
      if (url.hostname !== 'passport.test') return route.abort();
      if (url.pathname === '/') return route.fulfill({contentType: 'text/html; charset=utf-8', body: fs.readFileSync(path.join(root, 'main/setup.html'), 'utf8')});
      if (url.pathname === '/qr.js' || url.pathname === '/backup.js') return route.fulfill({contentType: 'application/javascript', body:fs.readFileSync(path.join(root,url.pathname === '/qr.js'?'assets/qr/qrcodegen-v1.8.0-es6.js':'main/backup.js'),'utf8')});
      if (url.pathname === '/status') return route.fulfill({contentType:'application/json',body:JSON.stringify({firmware:'test',wifi:true,clock:true,connecting:false,syncing:false,wait:0,save:0,cos_missing:0,builtin_cos:true,codex_accounts:2,codex_status:1,cos_http:200})});
      if (url.pathname === '/config') {
        posted = route.request().postDataJSON();
        return route.fulfill({status: reject ? 400 : 200, contentType: 'application/json', body: reject ? '{}' : '{"accepted":true}'});
      }
      return route.abort();
    });
    await page.goto('http://passport.test/');
    assert.equal(await page.title(), 'AI 订阅详情 · 配置');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    fs.mkdirSync(path.join(root, 'build'), {recursive: true});
    await page.screenshot({path: path.join(root, 'build/setup-preview.png'), fullPage: true});
    await page.locator('[name=ssid]').fill('test-network');
    await page.locator('[name=password]').fill('test-password');
    assert.equal(await page.locator('[name=cline], [name=deepseek]').count(), 0);
    await page.getByRole('button', {name: '保存配置并同步'}).click();
    await page.locator('#result').filter({hasText: '配置已接收'}).waitFor();
    assert.deepEqual(posted, {ssid: 'test-network', password: 'test-password'});
    assert.equal('deepseek' in posted, false); // Omitted fields preserve configuration.
    await page.locator('[name=ssid]').fill('open-network');
    await page.locator('#open').check();
    reject = true;
    await page.getByRole('button', {name: '保存配置并同步'}).click();
    await page.locator('#result').filter({hasText: '提交失败'}).waitFor();
    assert.deepEqual(posted, {ssid: 'open-network', password: ''});
    assert.equal(await page.getByRole('button', {name: '保存配置并同步'}).isEnabled(), true);
    assert.deepEqual(errors, []);
    console.log('Setup page mobile layout, retention and error recovery: PASS');
  } finally {
    await browser.close();
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
