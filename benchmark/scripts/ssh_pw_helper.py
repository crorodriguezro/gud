#!/usr/bin/env python3
"""Minimal non-interactive password-auth SSH helper for lab hardware.

Reads the target and remote command from argv, and the password from the
PW environment variable (never printed, never logged, never written to
disk). Used only against the lab devices documented in
docs/oneplus6-usb-host-gud-troubleshooting.md.
"""
import os
import sys
import pexpect

def main():
    if len(sys.argv) < 3:
        print("usage: ssh_pw_helper.py user@host remote_command...", file=sys.stderr)
        return 2
    target = sys.argv[1]
    remote_cmd = " ".join(sys.argv[2:])
    pw = os.environ.get("PW")
    if not pw:
        print("PW env var not set", file=sys.stderr)
        return 2
    cmd = (
        "ssh -F /dev/null -o PreferredAuthentications=password "
        "-o PubkeyAuthentication=no -o KbdInteractiveAuthentication=no "
        "-o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 "
        f"{target} -- {remote_cmd}"
    )
    child = pexpect.spawn(cmd, timeout=30, encoding="utf-8", codec_errors="replace")
    try:
        idx = child.expect(["password:", pexpect.EOF, pexpect.TIMEOUT], timeout=15)
        if idx == 0:
            child.sendline(pw)
            child.expect(pexpect.EOF, timeout=60)
        out = child.before or ""
    except Exception as e:
        out = child.before or ""
        print(f"[helper error: {e}]", file=sys.stderr)
    print(out)
    child.close()
    return child.exitstatus or 0

if __name__ == "__main__":
    sys.exit(main())
