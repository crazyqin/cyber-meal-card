// Local JS interpreter. This process cannot fetch resources: all HTTP is Python-owned.
const fs=require('node:fs');
const path=require('node:path');
const Module=require('node:module');
const readline=require('node:readline');
// Preserve web API fingerprints while preventing actual sockets in this worker.
require('node:net').Socket.prototype.connect=function(){throw new Error('OfflineRuntime');};
require('node:tls').connect=function(){throw new Error('OfflineRuntime');};
const originalLoad=Module._load;
Module._load=function(name,parent,isMain){
  if(name==='canvas')return require('@napi-rs/canvas');
  return Reflect.apply(originalLoad,this,[name,parent,isMain]);
};
const {jsdomFromText}=require('./vendor/sdenv/utils/jsdom');
const jsdom=require('sdenv-jsdom');
const idl=require('sdenv-jsdom/lib/jsdom/living/generated/utils');
const ORIGIN='https://token.telecomjs.com';
const API_PATH='/api/heimdall-product/cost-center/customer/card-with-time-range';
const rl=readline.createInterface({input:process.stdin,crlfDelay:Infinity});
const write=value=>process.stdout.write(JSON.stringify(value)+'\n');
let dom,input,userRequest;
const nativeRequests=[];
const signedRequests=[];
let timeout;

function finish(){
  clearTimeout(timeout);
  rl.close();
  try{dom?.window.close();}catch{}
  process.exit(0);
}
function fail(error){
  // Avoid serializing exceptions that could contain request URLs or credentials.
  write({event:'error',kind:/^[A-Za-z]+$/.test(error?.name||'')?error.name:'RuntimeError'});
  finish();
}
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function cookies(){return dom.cookieJar.getCookiesSync(ORIGIN).map(c=>({name:c.key,value:c.value}));}
function safeURL(value){
  const url=new URL(value,ORIGIN);
  if(url.origin!==ORIGIN)throw new Error('UnexpectedOrigin');
  return url;
}

async function start(config){
  input=config;
  safeURL(input.documentUrl);
  const jar=new jsdom.CookieJar();
  for(const cookie of input.cookies||[]){
    jar.setCookieSync(`${cookie.name}=${cookie.value}; Path=/${cookie.httpOnly?'; HttpOnly':''}`,ORIGIN);
  }
  class OfflineResources extends jsdom.ResourceLoader {
    fetch(url){
      const source=input.scripts?.[url];
      if(safeURL(url).origin!==ORIGIN||typeof source!=='string')return null;
      const result=Promise.resolve(Buffer.from(source));
      result.abort=()=>{};
      return result;
    }
  }
  dom=jsdomFromText(input.html,{
    url:input.documentUrl,referrer:input.documentUrl,runScripts:'dangerously',cookieJar:jar,
    resources:new OfflineResources({userAgent:input.userAgent,strictSSL:true}),
    consoleConfig:{log(){},warn(){},error(){},info(){},table(){}},
    windowProxyConfig:{windowGetterErrorKeys:['process'],windowGetterUndefinedKeys:['_runScripts']},
    beforeParse(win,sdenv){
      class NavigatorOverride {
        get userAgent(){return input.userAgent;}
        get appVersion(){return input.userAgent.replace('Mozilla/','');}
        get platform(){return 'Win32';}
      }
      sdenv.tools.mixin(win.navigator,NavigatorOverride.prototype,['userAgent','appVersion','platform']);
      const XHR=win.XMLHttpRequest;
      const originalOpen=XHR.prototype.open;
      const originalSetHeader=XHR.prototype.setRequestHeader;
      const state=new WeakMap();
      XHR.prototype.open=sdenv.tools.setFuncNative(function open(method,url,...rest){
        safeURL(url);state.set(this,{method,url,headers:{}});
        return Reflect.apply(originalOpen,this,[method,url,...rest]);
      },'open',2);
      XHR.prototype.setRequestHeader=sdenv.tools.setFuncNative(function setRequestHeader(name,value){
        state.get(this).headers[name]=value;
        return Reflect.apply(originalSetHeader,this,[name,value]);
      },'setRequestHeader',2);
      XHR.prototype.send=sdenv.tools.setFuncNative(function send(body){
        signedRequests.push({...state.get(this),body});nativeRequests.push(this);
      },'send',1);
      win.HTMLFormElement.prototype.submit=sdenv.tools.setFuncNative(function submit(){},'submit',0);
    },
  });
  timeout=setTimeout(()=>fail(new Error('RuntimeTimeout')),input.timeoutMs||30000);
  await delay(1000);
  if(input.mode==='bootstrap'){
    write({event:'cookies',cookies:cookies()});finish();return;
  }
  const request=input.request;
  if(safeURL(request.url).pathname!==API_PATH||request.method!=='POST')throw new Error('UnexpectedRequest');
  userRequest=new dom.window.XMLHttpRequest();
  userRequest.open(request.method,request.url,true);
  for(const [name,value] of Object.entries(request.headers||{}))userRequest.setRequestHeader(name,value);
  userRequest.send(request.body);
  if(signedRequests.length!==1)throw new Error('UnexpectedRequestCount');
  write({event:'signed',request:signedRequests[0],cookies:cookies()});
}

async function receive(response){
  const raw=nativeRequests.at(-1);
  const impl=idl.implForWrapper(raw);
  const body=Buffer.from(response.body,'base64');
  impl.status=response.status;impl.statusText=response.status===200?'OK':'Error';
  impl.responseURL=safeURL(response.responseURL).href;
  Object.assign(impl.properties,{
    responseBuffer:body,totalReceivedChunkSize:body.length,
    responseHeaders:Object.fromEntries(Object.entries(response.headers).map(([k,v])=>[k.toLowerCase(),v])),
    responseCache:null,responseTextCache:null,responseXMLCache:null,send:true,error:'',
  });
  for(const readyState of [2,3,4]){
    impl.readyState=readyState;raw.dispatchEvent(new dom.window.Event('readystatechange'));
  }
  raw.dispatchEvent(new dom.window.ProgressEvent('load'));
  raw.dispatchEvent(new dom.window.ProgressEvent('loadend'));
  await delay(500);
  write({event:'decoded',status:userRequest.status,body:Buffer.from(userRequest.responseText).toString('base64'),cookies:cookies()});
  finish();
}
rl.on('line',line=>{
  try{
    const value=JSON.parse(line);
    Promise.resolve(value.event==='start'?start(value):value.event==='response'?receive(value):Promise.reject(new Error('UnknownCommand'))).catch(fail);
  }catch(error){fail(error);}
});
process.on('uncaughtException',fail);
process.on('unhandledRejection',fail);
