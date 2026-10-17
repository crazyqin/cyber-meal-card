"""HTTP transport plus an offline JavaScript protocol worker; no browser launch."""
from __future__ import annotations

import base64
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
from urllib.parse import urljoin, urlsplit

ROOT=Path(__file__).resolve().parent
ORIGIN="https://token.telecomjs.com"
PAGE_URL=ORIGIN+"/finance/cost"
API_URL=ORIGIN+"/api/heimdall-product/cost-center/customer/card-with-time-range"
USER_AGENT="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"


class ProtocolError(RuntimeError):
    pass


class ScriptTags(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls=[]

    def handle_starttag(self,tag,attrs):
        data=dict(attrs)
        if tag.lower()=="script" and data.get("src") and data.get("type")!="module":
            self.urls.append(data["src"])


def checked_url(value: str, path: str | None=None) -> str:
    url=urljoin(ORIGIN,value)
    parts=urlsplit(url)
    if parts.scheme!="https" or parts.netloc!="token.telecomjs.com" or (path and parts.path!=path):
        raise ProtocolError("站点请求地址不符合预期，已停止。")
    return url


def node_executable() -> str:
    value=os.environ.get("TOKENHUB_NODE") or shutil.which("node")
    if not value:
        try:
            value=json.loads((ROOT/".runtime-config.json").read_text(encoding="utf-8")).get("node_path")
        except (OSError,ValueError):
            pass
    if not value or not Path(value).is_file():
        raise ProtocolError("未找到 Node.js 24，请安装或设置 TOKENHUB_NODE。")
    return str(value)


class JavaScriptWorker:
    def __init__(self,timeout,node_path=None):
        self.timeout=timeout
        self.messages=queue.Queue()
        from .sandbox import SandboxUnavailable, worker_process
        try:
            self.process=worker_process(node_path or node_executable(), ROOT/"waf_runtime/worker.cjs")
        except SandboxUnavailable:
            raise ProtocolError("sandbox_unavailable") from None
        def read():
            try:
                for line in self.process.stdout:
                    try:self.messages.put(json.loads(line))
                    except ValueError:continue
            finally:self.messages.put(None)
        threading.Thread(target=read,daemon=True).start()

    def send(self,value):
        try:
            self.process.stdin.write(json.dumps(value,ensure_ascii=False)+"\n")
            self.process.stdin.flush()
        except (OSError,ValueError) as error:
            raise ProtocolError("本地 JavaScript 校验进程已退出。") from error

    def receive(self,event):
        try:message=self.messages.get(timeout=self.timeout)
        except queue.Empty as error:raise ProtocolError("等待本地 JavaScript 校验超时。") from error
        if message is None:
            raise ProtocolError("sandbox_unavailable")
        if not isinstance(message,dict) or message.get("event")!=event:
            raise ProtocolError("本地 JavaScript 协议处理失败。")
        return message

    def close(self):
        if self.process.poll() is None:
            try:self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
        for pipe in (self.process.stdin,self.process.stdout):
            if pipe:
                try:pipe.close()
                except OSError:pass  # A closed pipe must not replace the original error.
        if hasattr(self.process, 'release'):self.process.release()

    def __enter__(self):return self
    def __exit__(self,*args):self.close()


class HttpProtocolClient:
    def __init__(self,timeout=30,use_env_proxy=False,node_path=None):
        try:
            from curl_cffi import requests, CurlOpt
            from curl_cffi.curl import DEFAULT_CACERT
        except ImportError as error:raise ProtocolError("请先安装依赖：python -m pip install -r requirements.txt") from error
        self.timeout=timeout
        self.node_path=node_path
        curl_options={}
        if os.name=="nt":
            # libcurl opens CA files through the Windows ANSI file API. Python's
            # -X utf8 changes locale.getpreferredencoding(), not that file API.
            # Pass encoded paths explicitly, preserving the selected CA bundle
            # and normal server/proxy certificate verification.
            try:ca_path=DEFAULT_CACERT.encode("mbcs")
            except UnicodeEncodeError as error:
                raise ProtocolError("证书目录含 Windows 系统编码不支持的字符。") from error
            curl_options={CurlOpt.CAINFO:ca_path,CurlOpt.PROXY_CAINFO:ca_path}
        self.session=requests.Session(impersonate="chrome150",trust_env=use_env_proxy,curl_options=curl_options)
        self.scripts={}
        self.document=None
        self.agent=USER_AGENT

    def request(self,method,url,**kwargs):
        checked_url(url)
        try:
            return self.session.request(method,url,timeout=self.timeout,allow_redirects=False,**kwargs)
        except Exception as error:
            raise ProtocolError("HTTP 连接失败或超时。") from error

    def cookie_state(self):
        return [{"name":c.name,"value":c.value,"httpOnly":"HttpOnly" in c._rest}
                for c in self.session.cookies.jar if c.domain.lstrip(".")=="token.telecomjs.com"]

    def apply_cookies(self,cookies):
        for cookie in cookies:
            if isinstance(cookie.get("name"),str) and isinstance(cookie.get("value"),str):
                self.session.cookies.set(cookie["name"],cookie["value"],domain="token.telecomjs.com",path="/")

    def runtime_input(self,response,mode,request=None):
        parser=ScriptTags();parser.feed(response.text)
        if not parser.urls:raise ProtocolError("校验页缺少 JavaScript 入口。")
        # The first non-module script is the protection runtime, before pagelog/app.
        url=checked_url(urljoin(response.url,parser.urls[0]))
        if url not in self.scripts:
            script=self.request("GET",url,headers={"User-Agent":self.agent,"Accept":"*/*","Referer":PAGE_URL})
            if script.status_code!=200 or len(script.content)>2_000_000:
                raise ProtocolError("站点校验脚本未能正常加载。")
            self.scripts[url]=script.text
        return {"event":"start","mode":mode,"html":response.text,"documentUrl":response.url,
                "userAgent":self.agent,"scripts":{url:self.scripts[url]},"cookies":self.cookie_state(),
                "request":request,"timeoutMs":int(self.timeout*1000)}

    def bootstrap(self):
        self.session.cookies.clear()
        headers={"User-Agent":self.agent,"Accept":"text/html,application/xhtml+xml,*/*;q=0.8",
                 "Sec-Fetch-Dest":"document","Sec-Fetch-Mode":"navigate","Sec-Fetch-Site":"none"}
        response=self.request("GET",PAGE_URL,headers=headers)
        if response.status_code==412:
            with JavaScriptWorker(self.timeout,self.node_path) as worker:
                worker.send(self.runtime_input(response,"bootstrap"))
                self.apply_cookies(worker.receive("cookies")["cookies"])
            response=self.request("GET",PAGE_URL,headers=headers)
        if response.status_code!=200 or 'id="app"' not in response.text:
            raise ProtocolError("HTTP 页面校验尚未通过。")
        self.document=response

    def fetch(self,auth,day):
        for attempt in range(2):
            status,body=self._fetch_once(auth,day)
            if status not in (400,412) or attempt:
                return status,body
            self.document=None
            self.scripts.clear()

    def _fetch_once(self,auth,day):
        if self.document is None:self.bootstrap()
        gray=auth.get("gray_flag","0")
        if gray not in ("0","1"):gray="0"
        self.session.cookies.set("grayFlag",gray,domain="token.telecomjs.com",path="/")
        original={"method":"POST","url":API_URL,"headers":{
            "Content-Type":"application/json","Satoken":auth["satoken"],"grayFlag":gray},
            "body":json.dumps({"startDate":day,"endDate":day,"adminView":False},separators=(",",":"))}
        with JavaScriptWorker(self.timeout,self.node_path) as worker:
            worker.send(self.runtime_input(self.document,"query",original))
            signed=worker.receive("signed")
            self.apply_cookies(signed["cookies"])
            request=signed["request"]
            url=checked_url(request["url"],urlsplit(API_URL).path)
            if request["method"].upper()!="POST":raise ProtocolError("费用查询方法发生变化。")
            headers={**request["headers"],"User-Agent":self.agent,"Origin":ORIGIN,"Referer":PAGE_URL,
                     "Accept":"application/json, text/plain, */*",
                     "Sec-Fetch-Site":"same-origin","Sec-Fetch-Mode":"cors","Sec-Fetch-Dest":"empty"}
            response=self.request("POST",url,data=request["body"],headers=headers)
            if response.status_code!=200:return response.status_code,response.content
            if response.content.lstrip().startswith(b"{"):return 200,response.content
            worker.send({"event":"response","status":response.status_code,"headers":dict(response.headers),
                         "body":base64.b64encode(response.content).decode(),"responseURL":url})
            decoded=worker.receive("decoded")
            self.apply_cookies(decoded["cookies"])
            return decoded["status"],base64.b64decode(decoded["body"],validate=True)

    def close(self):self.session.close()
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
