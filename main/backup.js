/* Offline backup transport. The QR text contains credentials; no network calls. */
(function(root){
'use strict';
const limits={ssid:32,password:64,cline:255,deepseek:255,cos_bucket:95,cos_region:47,cos_prefix:128,cos_secret_id:127,cos_secret_key:127,cos_token:2047};
function validate(input,full){
  if(!input||typeof input!=='object'||Array.isArray(input))throw Error('Invalid backup');
  if(full&&(input.app!=='ai-passport'||input.backup_schema!==1))throw Error('Unsupported backup');
  const result={};
  for(const [key,max] of Object.entries(limits)){
    if(!(key in input)){if(full)throw Error('Missing backup field');continue;}
    if(typeof input[key]!=='string'||new TextEncoder().encode(input[key]).length>max||/[\x00-\x1f\x7f]/.test(input[key]))throw Error('Invalid backup field');
    result[key]=input[key];
  }
  return result;
}
function checksum(text){let c=0xffffffff;for(const v of new TextEncoder().encode(text)){c^=v;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return ((c^0xffffffff)>>>0).toString(16).padStart(8,'0');}
function encode(backup){
  const data={app:'ai-passport',backup_schema:1,...validate(backup,true)};
  const bytes=new TextEncoder().encode(JSON.stringify(data));
  let binary='';for(const b of bytes)binary+=String.fromCharCode(b);
  const payload=btoa(binary),id=checksum(payload),count=Math.ceil(payload.length/1000),parts=[];
  if(count>16)throw Error('Backup too large');
  for(let i=0;i<count;i++)parts.push('AIP1/'+id+'/'+(i+1)+'/'+count+'/'+payload.slice(i*1000,(i+1)*1000));
  return parts;
}
function decode(lines){
  const parts=new Map();let id='',count=0;
  for(const line of lines.trim().split(/\s+/)){
    const m=/^AIP1\/([0-9a-f]{8})\/(\d+)\/(\d+)\/([A-Za-z0-9+/=]+)$/.exec(line);
    if(!m||m[4].length>1000)throw Error('Invalid QR part');
    const index=Number(m[2]),total=Number(m[3]);
    if(total<1||total>16||index<1||index>total||(id&&(id!==m[1]||count!==total)))throw Error('Mixed backups');
    id=m[1];count=total;if(parts.has(index)&&parts.get(index)!==m[4])throw Error('Conflicting QR part');parts.set(index,m[4]);
  }
  if(parts.size!==count)throw Error('Missing QR parts');
  let payload='';for(let i=1;i<=count;i++)payload+=parts.get(i);
  if(checksum(payload)!==id)throw Error('QR checksum mismatch');
  const binary=atob(payload),bytes=Uint8Array.from(binary,c=>c.charCodeAt(0));
  const backup=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
  return {app:'ai-passport',backup_schema:1,...validate(backup,true)};
}
root.PassportBackup={limits,validate,encode,decode};
if(typeof module==='object'&&module.exports)module.exports=root.PassportBackup;
})(typeof window==='undefined'?globalThis:window);
