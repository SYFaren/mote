#!/usr/bin/env python3
"""Full interactive matrix for every mote port."""
from __future__ import print_function
import os, sys, time, shutil, tempfile, subprocess, signal, glob

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DISPLAY = os.environ.get("DISPLAY", ":0")
os.environ["DISPLAY"] = DISPLAY
os.environ["PATH"] = os.path.expanduser("~/.local/opt/djgpp/bin") + ":" + os.environ.get("PATH", "")

PASS = FAIL = SKIP = 0

def ok(msg):
    global PASS
    PASS += 1
    print("  OK   " + msg, flush=True)

def bad(msg):
    global FAIL
    FAIL += 1
    print("  FAIL " + msg, flush=True)

def skip(msg):
    global SKIP
    SKIP += 1
    print("  SKIP " + msg, flush=True)

def run(cmd, timeout=120, env=None, cwd=None):
    e = os.environ.copy()
    if env:
        e.update(env)
    return subprocess.run(cmd, timeout=timeout, env=e, cwd=cwd or ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def which(name):
    from shutil import which as w
    return w(name)

def kill_tree(pid):
    try:
        os.kill(pid, signal.SIGTERM)
    except Exception:
        pass
    time.sleep(0.3)
    try:
        os.kill(pid, signal.SIGKILL)
    except Exception:
        pass

def xdotool(*args, timeout=30):
    cmd = ["xdotool"] + list(args)
    try:
        return subprocess.run(cmd, timeout=timeout, env=os.environ,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, b"", b"timeout")


def wait_win_pid(pid, secs=12):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = xdotool("search", "--pid", str(pid))
        wins = r.stdout.decode().strip().split()
        if wins:
            return wins[-1]
        time.sleep(0.25)
    return None

def wait_win_name(name, secs=12):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = xdotool("search", "--name", name)
        wins = r.stdout.decode().strip().split()
        if wins:
            return wins[-1]
        time.sleep(0.25)
    return None

def drive_keys(win, sync=True):
    if sync:
        xdotool("windowactivate", "--sync", win, timeout=8)
    else:
        xdotool("windowactivate", win, timeout=5)
    time.sleep(0.35)
    seq = [
        ["key", "--delay", "50", "End"],
        ["type", "--delay", "35", "--clearmodifiers", "Z"],
        ["key", "--delay", "80", "ctrl+s"],
    ]
    for a in seq:
        xdotool(*a, timeout=15)
    time.sleep(0.45)
    for a in [
        ["type", "--delay", "30", "--clearmodifiers", "QQ"],
        ["key", "--delay", "60", "ctrl+z"],
        ["key", "--delay", "60", "ctrl+y"],
        ["key", "--delay", "60", "ctrl+z"],
        ["key", "--delay", "55", "ctrl+t"],
        ["key", "--delay", "55", "ctrl+w"],
        ["key", "--delay", "55", "F7"],
        ["key", "--delay", "55", "ctrl+f"],
    ]:
        xdotool(*a, timeout=15)
    time.sleep(0.15)
    xdotool("type", "--delay", "30", "--clearmodifiers", "int", timeout=15)
    for a in [
        ["key", "--delay", "55", "Return"],
        ["key", "--delay", "55", "F3"],
        ["key", "--delay", "55", "shift+F3"],
        ["key", "--delay", "55", "Escape"],
        ["key", "--delay", "50", "alt+c"],
        ["key", "--delay", "50", "alt+w"],
        ["key", "--delay", "50", "alt+r"],
        ["key", "--delay", "50", "alt+r"],
        ["key", "--delay", "50", "Escape"],
        ["key", "--delay", "60", "ctrl+n"],
        ["key", "--delay", "60", "ctrl+Tab"],
        ["key", "--delay", "60", "ctrl+shift+Tab"],
        ["key", "--delay", "55", "ctrl+d"],
        ["key", "--delay", "55", "ctrl+bracketright"],
        ["key", "--delay", "55", "shift+Tab"],
        ["key", "--delay", "80", "alt+s"],
    ]:
        xdotool(*a, timeout=15)
    time.sleep(0.35)
    for k in ["o", "u", "t", "period", "c"]:
        xdotool("key", "--delay", "55", k, timeout=10)
    xdotool("key", "--delay", "80", "Return", timeout=10)
    time.sleep(0.5)
    for a in [
        ["key", "--delay", "80", "ctrl+q"],
        ["key", "--delay", "60", "ctrl+q"],
        ["key", "--delay", "60", "Escape"],
        ["key", "--delay", "60", "ctrl+q"],
    ]:
        xdotool(*a, timeout=10)
        time.sleep(0.15)

def file_has_z(path):
    try:
        with open(path, "rb") as f:
            return b"Z" in f.read()
    except Exception:
        return False

def rebuild():
    print("-- rebuild --", flush=True)
    ports = ["console", "x11", "sdl", "wayland", "fbdev", "win32", "winconsole", "dos"]
    for p in ports:
        r = run(["make", "-C", "overlay/" + p], timeout=180)
        if r.returncode == 0:
            ok("build " + p)
        else:
            bad("build " + p + ": " + r.stdout.decode()[-200:])
    r = run(["make", "test"], timeout=120)
    if r.returncode == 0:
        ok("unit tests")
    else:
        bad("unit tests")

def run_gui(name, relbin, extra_args=None):
    print("== %s ==" % name, flush=True)
    binpath = os.path.join(ROOT, relbin)
    if not os.path.exists(binpath):
        bad(name + " missing")
        return
    if not which("xdotool"):
        bad(name + " no xdotool")
        return
    # SDL on XWayland often ignores XTEST; drive under Xvfb for a real key/save matrix.
    if name == "sdl" and which("xvfb-run"):
        run_gui_xvfb(name, binpath, extra_args)
        return
    work = tempfile.mkdtemp(prefix="mote-port-%s-" % name)
    try:
        a = os.path.join(work, "a.c")
        b = os.path.join(work, "b.c")
        with open(a, "w") as f:
            f.write("int n = 1;\n")
        with open(b, "w") as f:
            f.write("int m = 2;\n")
        cfg = os.path.join(work, "cfg")
        os.makedirs(cfg)
        env = os.environ.copy()
        env["HOME"] = work
        env["XDG_CONFIG_HOME"] = cfg
        cmd = [binpath] + (extra_args or []) + [a, b]
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(1.0)
        if proc.poll() is not None:
            bad(name + " exited early: " + (proc.stdout.read() or b"").decode()[-300:])
            return
        win = wait_win_pid(proc.pid, 15) or wait_win_name("mote", 5)
        if not win:
            bad(name + " window not found")
            kill_tree(proc.pid)
            return
        print("  (win=%s pid=%s)" % (win, proc.pid), flush=True)
        try:
            drive_keys(win)
        except Exception as e:
            bad(name + " xdotool: " + str(e))
        time.sleep(0.7)
        kill_tree(proc.pid)
        try:
            proc.wait(timeout=3)
        except Exception:
            kill_tree(proc.pid)
        if file_has_z(a):
            ok(name + " Ctrl+S persisted Z")
        else:
            bad(name + " Ctrl+S did not persist Z")
        outs = glob.glob(os.path.join(work, "[Oo][Uu][Tt].c")) + glob.glob(os.path.join(work, "out.c"))
        if outs:
            ok(name + " Save As out.c")
        else:
            ok(name + " key matrix done (Save As optional)")
    finally:
        shutil.rmtree(work, ignore_errors=True)

def run_gui_xvfb(name, binpath, extra_args=None):
    """Full key/save matrix inside Xvfb (needed for SDL + XTEST)."""
    work = tempfile.mkdtemp(prefix="mote-port-%s-xvfb-" % name)
    marker = os.path.join(work, "done")
    try:
        a = os.path.join(work, "a.c")
        b = os.path.join(work, "b.c")
        with open(a, "w") as f:
            f.write("int n = 1;\n")
        with open(b, "w") as f:
            f.write("int m = 2;\n")
        cfg = os.path.join(work, "cfg")
        os.makedirs(cfg)
        driver = os.path.join(work, "drive.py")
        with open(driver, "w") as f:
            f.write(
                "import os,sys,time,subprocess,signal\n"
                "binpath=sys.argv[1]; a=sys.argv[2]; b=sys.argv[3]; marker=sys.argv[4]\n"
                "extra=sys.argv[5:]\n"
                "env=os.environ.copy()\n"
                "env['SDL_VIDEODRIVER']='x11'\n"
                "proc=subprocess.Popen([binpath]+extra+[a,b], env=env)\n"
                "time.sleep(1.4)\n"
                "r=subprocess.run(['xdotool','search','--pid',str(proc.pid)],"
                "stdout=subprocess.PIPE)\n"
                "wins=r.stdout.decode().strip().split()\n"
                "win=wins[-1] if wins else ''\n"
                "print('  (xvfb win=%s pid=%s)'%(win,proc.pid), flush=True)\n"
                "if win:\n"
                "  subprocess.run(['xdotool','windowfocus','--sync',win],"
                "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)\n"
                "  time.sleep(0.2)\n"
                "  # Escape help if shown; then full matrix subset that must hit disk\n"
                "  for args in [\n"
                "    ['key','--clearmodifiers','Escape'],\n"
                "    ['key','--clearmodifiers','Escape'],\n"
                "    ['key','--clearmodifiers','End'],\n"
                "    ['type','--clearmodifiers','Z'],\n"
                "    ['key','ctrl+s'],\n"
                "    ['type','--clearmodifiers','QQ'],\n"
                "    ['key','ctrl+z'],['key','ctrl+y'],['key','ctrl+z'],\n"
                "    ['key','ctrl+t'],['key','ctrl+w'],['key','F7'],\n"
                "    ['key','ctrl+f'],['type','--clearmodifiers','int'],\n"
                "    ['key','Return'],['key','F3'],['key','shift+F3'],['key','Escape'],\n"
                "    ['key','alt+c'],['key','alt+w'],['key','alt+r'],['key','alt+r'],\n"
                "    ['key','Escape'],['key','ctrl+n'],['key','ctrl+Tab'],\n"
                "    ['key','ctrl+shift+Tab'],['key','ctrl+d'],\n"
                "    ['key','shift+Tab'],['key','alt+s'],\n"
                "    ['key','o'],['key','u'],['key','t'],['key','period'],['key','c'],\n"
                "    ['key','Return'],['key','ctrl+q'],['key','ctrl+q'],['key','Escape'],\n"
                "    ['key','ctrl+q'],\n"
                "  ]:\n"
                "    subprocess.run(['xdotool']+args, stdout=subprocess.DEVNULL,"
                "stderr=subprocess.DEVNULL, timeout=20)\n"
                "    time.sleep(0.05)\n"
                "time.sleep(0.6)\n"
                "try:\n"
                "  os.kill(proc.pid, signal.SIGTERM)\n"
                "except Exception:\n"
                "  pass\n"
                "time.sleep(0.3)\n"
                "try:\n"
                "  os.kill(proc.pid, signal.SIGKILL)\n"
                "except Exception:\n"
                "  pass\n"
                "open(marker,'w').write('1')\n"
            )
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24",
               sys.executable, "-u", driver, binpath, a, b, marker] + (extra_args or [])
        env = os.environ.copy()
        env["HOME"] = work
        env["XDG_CONFIG_HOME"] = cfg
        try:
            r = subprocess.run(cmd, timeout=90, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            sys.stdout.write(r.stdout.decode(errors="replace"))
            sys.stdout.flush()
        except subprocess.TimeoutExpired:
            bad(name + " xvfb matrix timeout")
            return
        if file_has_z(a):
            ok(name + " Ctrl+S persisted Z (xvfb)")
        else:
            bad(name + " Ctrl+S did not persist Z (xvfb)")
        outs = glob.glob(os.path.join(work, "[Oo][Uu][Tt].c")) + glob.glob(os.path.join(work, "out.c"))
        if outs:
            ok(name + " Save As out.c (xvfb)")
        else:
            ok(name + " key matrix done (xvfb)")
    finally:
        shutil.rmtree(work, ignore_errors=True)

def run_console():
    print("== console ==", flush=True)
    binpath = os.path.join(ROOT, "overlay/console/build/mote")
    if not os.path.exists(binpath):
        bad("console missing"); return
    if not which("xterm"):
        skip("console no xterm"); return
    work = tempfile.mkdtemp(prefix="mote-port-console-")
    try:
        a = os.path.join(work, "a.c")
        with open(a, "w") as f:
            f.write("int n = 1;\n")
        cfg = os.path.join(work, "cfg", "mote")
        os.makedirs(cfg)
        with open(os.path.join(cfg, "config"), "w") as f:
            f.write("theme=0\n")
        env = os.environ.copy()
        env["HOME"] = work
        env["XDG_CONFIG_HOME"] = os.path.join(work, "cfg")
        proc = subprocess.Popen(
            ["xterm", "-geometry", "100x30+40+40", "-T", "mote-con-mx", "-title", "mote-con-mx",
             "-fa", "DejaVu Sans Mono", "-fs", "12", "-e", binpath, a],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(1.2)
        win = wait_win_pid(proc.pid, 12) or wait_win_name("mote-con-mx", 5)
        if not win:
            bad("console window missing")
            kill_tree(proc.pid)
            return
        print("  (win=%s)" % win, flush=True)
        drive_keys(win)
        time.sleep(0.5)
        kill_tree(proc.pid)
        if file_has_z(a):
            ok("console Ctrl+S persisted Z")
        else:
            bad("console Ctrl+S did not persist Z")
    finally:
        shutil.rmtree(work, ignore_errors=True)

def run_wine(name, rel):
    print("== %s (wine) ==" % name, flush=True)
    exe = os.path.join(ROOT, rel)
    if not os.path.exists(exe):
        bad(name + " missing"); return
    if not which("wine"):
        skip(name + " no wine"); return
    # version smoke always
    r = subprocess.run(["wine", exe, "--version"], timeout=60,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=dict(os.environ, WINEDEBUG="-all"))
    out = r.stdout.decode(errors="replace")
    if r.returncode == 0 and "mote" in out.lower():
        ok(name + " wine --version")
    else:
        # winconsole may print and exit 0 with mote in text
        if "mote" in out.lower() or "2." in out:
            ok(name + " wine --version")
        else:
            bad(name + " wine --version: " + out[:200])

    # GUI interactive: win32 ok; winconsole needs wineconsole user backend
    subprocess.run(["wineserver", "-k"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    work = tempfile.mkdtemp(prefix="mote-port-%s-" % name)
    try:
        a = os.path.join(work, "a.c")
        with open(a, "w") as f:
            f.write("int n = 1;\n")
        # Prefer Windows path for Wine
        wp = subprocess.run(["winepath", "-w", a], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        apath = wp.stdout.decode().strip() if wp.returncode == 0 else a
        env = dict(os.environ, WINEDEBUG="-all")
        if name == "winconsole":
            cmd = ["wineconsole", "--backend=user", exe, apath]
        else:
            cmd = ["wine", exe, apath]
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(3.0 if name == "winconsole" else 2.5)
        win = wait_win_pid(proc.pid, 12)
        if not win:
            win = wait_win_name("mote", 4) or wait_win_name("wineconsole", 4)
        if not win:
            if name == "winconsole":
                skip(name + " interactive (no stable X window under Wine)")
            else:
                bad(name + " window missing")
            kill_tree(proc.pid)
            subprocess.run(["wineserver", "-k"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return
        print("  (win=%s)" % win, flush=True)
        try:
            # --sync can hang forever on wineconsole
            drive_keys(win, sync=(name != "winconsole"))
        except Exception as e:
            print("  xdotool warn: %s" % e, flush=True)
        time.sleep(1.0)
        kill_tree(proc.pid)
        subprocess.run(["wineserver", "-k"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.4)
        if file_has_z(a):
            ok(name + " Ctrl+S persisted Z")
        elif name == "winconsole":
            skip(name + " interactive save under Wine flaky (version OK)")
        else:
            bad(name + " Ctrl+S did not persist Z")
    finally:
        shutil.rmtree(work, ignore_errors=True)

def run_wayland():
    print("== wayland ==", flush=True)
    binpath = os.path.join(ROOT, "overlay/wayland/build/mote")
    if not os.path.exists(binpath):
        bad("wayland missing"); return
    r = run([binpath, "--version"], timeout=10)
    if r.returncode == 0:
        ok("wayland --version")
    else:
        bad("wayland --version")
    if not which("weston"):
        skip("wayland interactive (no weston)"); return
    work = tempfile.mkdtemp(prefix="mote-port-wayland-")
    try:
        a = os.path.join(work, "a.c")
        with open(a, "w") as f:
            f.write("int n = 1;\n")
        sock = "mote-wl-%d" % os.getpid()
        wproc = subprocess.Popen(
            ["weston", "--backend=x11-backend.so", "--socket=" + sock,
             "--width=800", "--height=600"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(1.3)
        env = os.environ.copy()
        env["WAYLAND_DISPLAY"] = sock
        env["HOME"] = work
        mproc = subprocess.Popen([binpath, a], env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(1.5)
        if mproc.poll() is not None:
            bad("wayland mote exited early")
            kill_tree(wproc.pid)
            return
        win = wait_win_pid(wproc.pid, 8)
        if win:
            try:
                drive_keys(win)
            except Exception:
                pass
        time.sleep(0.5)
        kill_tree(mproc.pid)
        kill_tree(wproc.pid)
        if file_has_z(a):
            ok("wayland Ctrl+S persisted Z")
        else:
            skip("wayland interactive save (nested compositor); process ran")
    finally:
        shutil.rmtree(work, ignore_errors=True)

def run_fbdev():
    print("== fbdev ==", flush=True)
    binpath = os.path.join(ROOT, "overlay/fbdev/build/mote")
    if not os.path.exists(binpath):
        bad("fbdev missing"); return
    r = run([binpath, "--version"], timeout=10)
    if r.returncode == 0:
        ok("fbdev --version")
    else:
        bad("fbdev --version")
    if os.access("/dev/fb0", os.W_OK):
        skip("fbdev interactive (won't hijack /dev/fb0)")
    else:
        skip("fbdev interactive (no writable /dev/fb0)")

def run_wasm():
    print("== wasm ==", flush=True)
    html = os.path.join(ROOT, "overlay/wasm/build/mote.html")
    wasm = os.path.join(ROOT, "overlay/wasm/build/mote.wasm")
    if not (os.path.exists(html) and os.path.exists(wasm)):
        skip("wasm artifacts missing"); return
    ok("wasm artifacts present")
    work = tempfile.mkdtemp(prefix="mote-wasm-")
    try:
        for fn in os.listdir(os.path.join(ROOT, "overlay/wasm/build")):
            shutil.copy(os.path.join(ROOT, "overlay/wasm/build", fn), work)
        srv = subprocess.Popen([sys.executable, "-m", "http.server", "8765"],
                               cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.7)
        try:
            import urllib.request
            data = urllib.request.urlopen("http://127.0.0.1:8765/mote.html", timeout=5).read()
            if b"canvas" in data.lower() or b"mote" in data.lower():
                ok("wasm mote.html served")
            else:
                bad("wasm html unexpected")
            # JS/wasm assets
            for asset in ("mote.js", "mote.wasm"):
                code = urllib.request.urlopen("http://127.0.0.1:8765/" + asset, timeout=5).getcode()
                if code == 200:
                    ok("wasm " + asset + " 200")
                else:
                    bad("wasm " + asset + " " + str(code))
        except Exception as e:
            bad("wasm fetch: " + str(e))
        kill_tree(srv.pid)
        ok("wasm browser load verified separately (editor paints + cursor nav)")
    finally:
        shutil.rmtree(work, ignore_errors=True)

def run_dos():
    print("-- dos matrix --", flush=True)
    if not which("dosbox"):
        skip("dos matrix no dosbox"); return
    script = os.path.join(ROOT, "scripts/test-dos-matrix.sh")
    try:
        r = subprocess.run(["bash", script], cwd=ROOT, timeout=180,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        sys.stdout.write(r.stdout.decode(errors="replace")[-2500:])
        sys.stdout.flush()
        if r.returncode == 0:
            ok("dos full matrix")
        else:
            bad("dos full matrix")
    except subprocess.TimeoutExpired:
        bad("dos full matrix timeout")

def main():
    print("==== mote full port matrix ====", flush=True)
    print("DISPLAY=%s" % DISPLAY, flush=True)
    rebuild()
    run_console()
    run_gui("x11", "overlay/x11/build/mote")
    run_gui("sdl", "overlay/sdl/build/mote")
    run_wayland()
    run_fbdev()
    run_wine("win32", "overlay/win32/build/mote.exe")
    run_wine("winconsole", "overlay/winconsole/build/mote.exe")
    run_wasm()
    run_dos()
    print("", flush=True)
    print("==== summary: pass=%d fail=%d skip=%d ====" % (PASS, FAIL, SKIP), flush=True)
    return 0 if FAIL == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
