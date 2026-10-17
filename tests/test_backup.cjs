/* Pure offline codec and QR-capacity tests; no browser or provider requests. */
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const backup=require('../main/backup.js');
for(const match of fs.readFileSync('main/setup.html','utf8').matchAll(/<script>([\s\S]*?)<\/script>/g))new vm.Script(match[1]);
const data={app:'ai-passport',backup_schema:1,ssid:'家庭 Wi-Fi',password:'example-password',cline:'example-cline',deepseek:'example-deepseek',cos_bucket:'example-1250000000',cos_region:'ap-shanghai',cos_prefix:'codex-usage',cos_secret_id:'AKIDEXAMPLE',cos_secret_key:'example-key',cos_token:''};
let parts=backup.encode(data);assert.deepEqual(backup.decode(parts.join('\n')),data);
const large={...data,cline:'c'.repeat(255),deepseek:'d'.repeat(255),cos_token:'t'.repeat(2047)};
parts=backup.encode(large);assert(parts.length>1);assert.deepEqual(backup.decode(parts.toReversed().join('\n')),large);
assert.throws(()=>backup.decode(parts.slice(1).join('\n')));
assert.throws(()=>backup.decode(parts.join('\n').replace(/.$/,'X')));
const other=backup.encode({...large,ssid:'other-network'});
assert.throws(()=>backup.decode([parts[0],other[1],...parts.slice(2)].join('\n')));
assert.throws(()=>backup.encode({...data,backup_schema:9}));
assert.throws(()=>backup.encode({...data,ssid:'家'.repeat(11)}));
assert.throws(()=>backup.encode({...data,cos_token:'x'.repeat(2048)}));
assert.deepEqual(backup.decode(backup.encode({...data,password:'',cline:'',deepseek:''}).join('\n')),{...data,password:'',cline:'',deepseek:''});
const context={};vm.runInNewContext(fs.readFileSync('assets/qr/qrcodegen-v1.8.0-es6.js','utf8'),context);
for(const part of parts){const qr=context.qrcodegen.QrCode.encodeText(part,context.qrcodegen.QrCode.Ecc.MEDIUM);assert(qr.size<=177&&qr.size>=21);}
console.log('Offline backup QR: UTF-8 roundtrip, multipart, integrity, empty-field restore and QR capacity: PASS');
