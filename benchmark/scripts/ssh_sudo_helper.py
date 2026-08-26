#!/usr/bin/env python3
"""Run a privileged remote command via SSH+sudo using password auth via
pexpect with a forced pty (-tt), answering both the SSH login password
prompt and the sudo password prompt. Reads PW (login password) and
SUDO_PW (sudo password, often same as PW) from environment. Never prints
passwords. Used only against the lab devices documented in
docs/oneplus6-usb-host-gud-troubleshooting.md.
"""
import os
import sys
import pexpect


def main():
    if len(sys.argv) < 3:
        print("usage: ssh_sudo_helper.py user@host remote_command...", file=sys.stderr)
        return 2
    target = sys.argv[1]
    remote_cmd = " ".join(sys.argv[2:])
    pw = os.environ.get("PW")
    sudo_pw = os.environ.get("SUDO_PW", pw)
    if not pw:
        print("PW env var not set", file=sys.stderr)
        return 2
    full_remote = f"sudo -S -p SUDOPWPROMPT {remote_cmd}"
    cmd = (
        "ssh -tt -F /dev/null -o PreferredAuthentications=password "
        "-o PubkeyAuthentication=no -o KbdInteractiveAuthentication=no "
        "-o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 "
        f"{target} -- {full_remote}"
    )
    child = pexpect.spawn(cmd, timeout=60, encoding="utf-8", codec_errors="replace")
    try:
        idx = child.expect(["password:", pexpect.EOF, pexpect.TIMEOUT], timeout=15)
        if idx == 0:
            child.sendline(pw)
            idx2 = child.expect(["SUDOPWPROMPT", pexpect.EOF, pexpect.TIMEOUT], timeout=15)
            if idx2 == 0:
                child.sendline(sudo_pw)
        child.expect(pexpect.EOF, timeout=90)
        out = child.before or ""
    except Exception as e:
        out = (child.before or "") + f"\n[helper error: {e}]"
    print(out)
    child.close(force=True)
    return child.exitstatus or 0


if __name__ == "__main__":
    sys.exit(main())
