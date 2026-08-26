#!/usr/bin/env python3
"""Open a persistent SSH ControlMaster connection using password auth via
pexpect, then exit (leaving the background master running so subsequent
plain `ssh -S <socket>` calls need no further password). Never prints the
password. PW env var required.
"""
import os, sys, pexpect

def main():
    target = sys.argv[1]
    socket_path = sys.argv[2]
    pw = os.environ.get("PW")
    if not pw:
        print("PW env var not set", file=sys.stderr); return 2
    cmd = (
        "ssh -F /dev/null -o PreferredAuthentications=password "
        "-o PubkeyAuthentication=no -o KbdInteractiveAuthentication=no "
        "-o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 "
        f"-M -S {socket_path} -fN {target}"
    )
    child = pexpect.spawn(cmd, timeout=20, encoding="utf-8", codec_errors="replace")
    idx = child.expect(["password:", pexpect.EOF, pexpect.TIMEOUT], timeout=15)
    if idx == 0:
        child.sendline(pw)
        child.expect(pexpect.EOF, timeout=15)
    print("master connection requested (background)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
