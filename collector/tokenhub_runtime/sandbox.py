"""OS isolation for downloaded JavaScript. Never fall back to an unrestricted Node."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

_prepared = set()


class SandboxUnavailable(RuntimeError):
    pass


def worker_process(node, worker):
    node, worker = Path(node).resolve(), Path(worker).resolve()
    runtime = worker.parent
    # Node permissions are an extra guard, not the OS security boundary.
    args = [str(node), '--permission', '--allow-addons', '--no-warnings',
            '--preserve-symlinks', '--preserve-symlinks-main',
            '--allow-fs-read=' + str(runtime), str(worker)]
    env = {k: os.environ[k] for k in ('SystemRoot', 'WINDIR', 'SystemDrive', 'USERPROFILE',
           'LOCALAPPDATA', 'APPDATA', 'TEMP', 'TMP', 'LANG') if k in os.environ}
    env.update(HOME='/nonexistent', NODE_ENV='production', CYBER_WAF_SANDBOX='1')
    if os.name == 'nt':
        return _windows(args, env, runtime, node)
    if sys.platform != 'linux' or not shutil.which('bwrap'):
        raise SandboxUnavailable('sandbox_unavailable')
    command = ['bwrap', '--unshare-all', '--die-with-parent', '--new-session', '--cap-drop', 'ALL',
               '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp']
    # System libraries/fonts only. Do not mount /home, account files, state or /etc.
    for directory in ('/lib', '/lib64', '/usr/lib', '/usr/lib64', '/usr/share/fonts',
                      '/usr/share/fontconfig', '/usr/share/zoneinfo'):
        if Path(directory).exists():
            command += ['--ro-bind', directory, directory]
    for directory in ('/etc/fonts', '/etc/ld.so.cache'):
        if Path(directory).exists():
            command += ['--ro-bind', directory, directory]
    command += ['--ro-bind', str(runtime), str(runtime), '--ro-bind', str(node), str(node),
                '--chdir', str(runtime), '--', *args]
    return subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, encoding='utf-8', env=env)


def _windows(args, env, runtime, node):
    """Capability-free AppContainer with read-only code ACLs."""
    import ctypes as c
    from ctypes import wintypes as w
    import msvcrt

    kernel = c.WinDLL('kernel32', use_last_error=True)
    userenv = c.WinDLL('userenv', use_last_error=True)
    advapi = c.WinDLL('advapi32', use_last_error=True)
    pointer = c.c_void_p

    class Startup(c.Structure):
        _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
                    ('x', w.DWORD), ('y', w.DWORD), ('xs', w.DWORD), ('ys', w.DWORD),
                    ('xc', w.DWORD), ('yc', w.DWORD), ('fill', w.DWORD), ('flags', w.DWORD),
                    ('show', w.WORD), ('reserved_size', w.WORD), ('reserved_ptr', pointer),
                    ('stdin', w.HANDLE), ('stdout', w.HANDLE), ('stderr', w.HANDLE)]

    class StartupEx(c.Structure):
        _fields_ = [('startup', Startup), ('attributes', pointer)]

    class Info(c.Structure):
        _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]

    class Capabilities(c.Structure):
        _fields_ = [('sid', pointer), ('capabilities', pointer), ('count', w.DWORD), ('reserved', w.DWORD)]

    userenv.CreateAppContainerProfile.argtypes = [w.LPCWSTR, w.LPCWSTR, w.LPCWSTR, pointer, w.DWORD, c.POINTER(pointer)]
    userenv.DeriveAppContainerSidFromAppContainerName.argtypes = [w.LPCWSTR, c.POINTER(pointer)]
    advapi.ConvertSidToStringSidW.argtypes = [pointer, c.POINTER(w.LPWSTR)]
    kernel.InitializeProcThreadAttributeList.argtypes = [pointer, w.DWORD, w.DWORD, c.POINTER(c.c_size_t)]
    kernel.UpdateProcThreadAttribute.argtypes = [pointer, w.DWORD, c.c_size_t, pointer, c.c_size_t, pointer, pointer]
    kernel.DeleteProcThreadAttributeList.argtypes = [pointer]
    kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, pointer, pointer, w.BOOL, w.DWORD, pointer,
                                      w.LPCWSTR, c.POINTER(StartupEx), c.POINTER(Info)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.LocalFree.argtypes = [pointer]
    advapi.FreeSid.argtypes = [pointer]
    name = 'CyberMealCard.WAF.' + hashlib.sha256(str(runtime).encode()).hexdigest()[:16]
    sid = pointer()
    result = userenv.CreateAppContainerProfile(name, name, 'Offline quota protocol worker', None, 0, c.byref(sid))
    if result != 0 and userenv.DeriveAppContainerSidFromAppContainerName(name, c.byref(sid)) != 0:
        raise SandboxUnavailable('sandbox_profile')
    attributes = None
    fds = []
    try:
        sid_text = w.LPWSTR()
        if not advapi.ConvertSidToStringSidW(sid, c.byref(sid_text)):
            raise SandboxUnavailable('sandbox_sid')
        try:
            identity = sid_text.value
        finally:
            kernel.LocalFree(c.cast(sid_text, pointer))
        # Grant only this package SID access to immutable runtime code. No grant
        # is made to collector/, credentials, personal directories or collected data.
        grants = [(runtime, '(OI)(CI)(RX)'), (node, '(RX)')]
        for directory, access in grants:
            key = (identity, directory, access)
            if key in _prepared:
                continue
            result = subprocess.run(['icacls.exe', str(directory), '/grant', '*' + identity + ':' + access, '/Q'],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            if result.returncode:
                raise SandboxUnavailable('sandbox_acl')
            _prepared.add(key)
        size = c.c_size_t()
        kernel.InitializeProcThreadAttributeList(None, 1, 0, c.byref(size))
        attributes = c.create_string_buffer(size.value)
        if not kernel.InitializeProcThreadAttributeList(attributes, 1, 0, c.byref(size)):
            raise SandboxUnavailable('sandbox_attributes')
        caps = Capabilities(sid, None, 0, 0)
        for key, value in ((0x20009, caps),):
            if not kernel.UpdateProcThreadAttribute(attributes, 0, key, c.byref(value), c.sizeof(value), None, None):
                raise SandboxUnavailable('sandbox_capabilities')
        in_read, in_write = os.pipe()
        out_read, out_write = os.pipe()
        err = os.open(os.devnull, os.O_WRONLY)
        fds = [in_read, in_write, out_read, out_write, err]
        for fd in (in_read, out_write, err):
            os.set_inheritable(fd, True)
        startup = StartupEx()
        startup.startup.cb = c.sizeof(startup)
        startup.startup.flags = 0x100  # STARTF_USESTDHANDLES
        startup.startup.stdin = msvcrt.get_osfhandle(in_read)
        startup.startup.stdout = msvcrt.get_osfhandle(out_write)
        startup.startup.stderr = msvcrt.get_osfhandle(err)
        startup.attributes = c.cast(attributes, pointer)
        info = Info()
        environment = c.create_unicode_buffer('\0'.join(k + '=' + v for k, v in sorted(env.items())) + '\0\0')
        if not kernel.CreateProcessW(args[0], c.create_unicode_buffer(subprocess.list2cmdline(args)), None, None, True,
                                     0x08080000 | 0x400, environment, str(runtime), c.byref(startup), c.byref(info)):
            raise SandboxUnavailable('sandbox_launch_' + str(c.get_last_error()))
        kernel.CloseHandle(info.thread)
        for fd in (in_read, out_write, err):
            os.close(fd)
            fds.remove(fd)

        class Process:
            pid = info.pid
            stdin = os.fdopen(in_write, 'w', encoding='utf-8', buffering=1)
            stdout = os.fdopen(out_read, 'r', encoding='utf-8')

            def poll(self):
                value = w.DWORD()
                if not kernel.GetExitCodeProcess(info.process, c.byref(value)):
                    return 1
                return None if value.value == 259 else value.value

            def wait(self, timeout=None):
                result = kernel.WaitForSingleObject(info.process, 0xFFFFFFFF if timeout is None else int(timeout * 1000))
                if result == 258:
                    raise subprocess.TimeoutExpired(args, timeout)
                return self.poll()

            def terminate(self):
                kernel.TerminateProcess(info.process, 1)

            kill = terminate

            def release(self):
                kernel.CloseHandle(info.process)

        fds.clear()
        return Process()
    finally:
        for fd in fds:
            os.close(fd)
        if attributes is not None:
            kernel.DeleteProcThreadAttributeList(attributes)
        advapi.FreeSid(sid)
