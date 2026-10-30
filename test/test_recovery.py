#!/usr/bin/env python3
"""POSIX PTY integration checks; never use the user's config/recovery files."""
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import time


def run():
    binary = str(Path('overlay/console/build/mote').resolve())
    active = []
    with tempfile.TemporaryDirectory(prefix='mote-recovery-') as directory:
        root = Path(directory)
        env = dict(os.environ, XDG_CONFIG_HOME=directory, TERM='xterm')

        def read(master, needle=None):
            result = b''
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if select.select([master], [], [], 0.1)[0]:
                    try:
                        result += os.read(master, 65536)
                    except OSError:
                        break
                    if needle and needle in result:
                        return result
                elif not needle:
                    break
            if needle:
                raise AssertionError(f'missing {needle!r}: {result[-500:]!r}')
            return result

        def start(needle=b'F1 help'):
            master, slave = pty.openpty()
            process = subprocess.Popen([binary], stdin=slave, stdout=slave,
                                       stderr=slave, env=env)
            os.close(slave)
            active.append((process, master))
            read(master, needle)
            return process, master

        def wait_file(path, contents):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if path.exists() and contents in path.read_bytes():
                    return
                time.sleep(0.02)
            raise AssertionError(f'checkpoint not written: {path}')

        try:
            process, master = start()
            os.write(master, b'recovery test')
            snapshot = root / 'mote/config.recovery-0'
            wait_file(snapshot, b'recovery test')
            assert snapshot.stat().st_mode & 0o777 == 0o600

            second, second_master = start()
            os.write(second_master, b'second instance')
            second_snapshot = root / 'mote/config.recovery-1'
            wait_file(second_snapshot, b'second instance')
            second.kill()
            second.wait(timeout=5)
            process.kill()
            process.wait(timeout=5)

            process, master = start(b'Recovery found')
            os.write(master, b'\r')
            read(master, b'Recovered; review and save')
            os.write(master, b'\x13')
            read(master, b'Save As:')
            target = root / 'restored.txt'
            os.write(master, os.fsencode(target))
            read(master, b'restored.txt')
            os.write(master, b'\r')
            read(master, b'saved')
            assert target.read_bytes() == b'recovery test'
            os.write(master, b'\x11')
            process.wait(timeout=5)
            assert not snapshot.exists()
            assert second_snapshot.exists()

            # Find stale slot 1 even though slot 0 is empty. Esc preserves it.
            process, master = start(b'Recovery found')
            os.write(master, b'\x1b')
            process.wait(timeout=5)
            assert second_snapshot.exists()
            process, master = start(b'Recovery found')
            os.write(master, b'\x11')
            read(master, b'F1 help')
            os.write(master, b'\x11')
            process.wait(timeout=5)
            assert not second_snapshot.exists()
        finally:
            for process, master in active:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                os.close(master)
    print('PTY recovery: crash, restore, permissions, concurrent slots, keep/discard OK')


if __name__ == '__main__':
    run()
