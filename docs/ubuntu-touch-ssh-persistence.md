# Persistent SSH on the OnePlus 6 Ubuntu Touch phone

## Scope

This documents the SSH boot-persistence failure diagnosed on the OnePlus 6
Ubuntu Touch phone at `192.168.1.120` on 2026-07-24, the failed approaches, the
actual filesystem-ordering cause, and the installed repair.

The UBports SSH guide recommends key authentication and systemd socket
activation. It documents `sudo systemctl enable ssh.socket` for boot startup:

<https://docs.ubports.com/en/latest/userguide/advanceduse/ssh.html>

## Symptoms

- Starting SSH manually worked.
- `systemctl enable ssh` or `systemctl enable ssh.socket` created persistent
  symlinks and reported `enabled`.
- After a real reboot, Wi-Fi returned but TCP port 22 returned
  `Connection refused`.
- Running `sudo systemctl enable ssh --now` locally restored SSH immediately.
- A custom service enabled under `multi-user.target` also survived on disk but
  was never queued at boot and had no journal entry.

## Root cause

The root filesystem (`system_a`) is mounted read-only. Ubuntu Touch later bind
mounts the persistent directory below over `/etc/systemd/system`:

```text
/dev/sda17[/system-data/etc/systemd/system] -> /etc/systemd/system
```

That persistent mount preserves units and enablement symlinks across reboots,
but PID 1 builds its initial target dependency graph before the bind-mounted
directory is visible. Consequently, custom units and `*.target.wants` links in
the persistent `/etc/systemd/system` appear after systemd has already assembled
the boot transaction. They report `enabled` after boot but were not considered
while the boot targets were started.

Evidence from the failed reboot:

- Kernel/system boot began at `2026-07-24 08:50:49`.
- `sockets.target` was reached at `08:51:02` without `ssh.socket`.
- `multi-user.target` was reached at `08:52:42` without the custom guard.
- There was no start, skip, condition, or failure journal record for the guard.
- Manual `systemctl enable ssh --now` started the socket and daemon at
  `08:53:29`.

The `lxc-android-config` package adds another complication: its preset disables
`ssh.service` and `ssh.socket`, and its post-install script may disable them
again during an update. It also supplies
`/etc/ssh/sshd_config.d/50-lxc-android-config.conf` with
`PasswordAuthentication=no`.

## Installed repair

The repair places only the `ssh.service` enablement link in the underlying
read-only system filesystem—the filesystem systemd sees when it constructs the
initial boot transaction:

```text
/etc/systemd/system/multi-user.target.wants/ssh.service
    -> /usr/lib/systemd/system/ssh.service
```

The system partition was temporarily remounted writable, the single symlink
was created through a non-recursive bind view of `/`, and the partition was
immediately remounted read-only. Verification confirmed:

```text
lower-root link -> /usr/lib/systemd/system/ssh.service
root options    -> ro,relatime,data=ordered
```

The ineffective `gud-ssh-socket-persistence.service` was disabled and removed.
The persistent upper `/etc` enablement remains harmless, while the lower-root
link is what makes the daemon visible during early boot.

## Reproducing the repair

Run this only with local/ADB access or another recovery path. Stopping SSH can
disconnect the current remote session.

```sh
sudo mkdir -p /tmp/ssh-rootfs-lower
sudo mount --bind / /tmp/ssh-rootfs-lower
sudo mount -o remount,rw /
sudo mount -o remount,bind,rw /tmp/ssh-rootfs-lower
sudo mkdir -p /tmp/ssh-rootfs-lower/etc/systemd/system/multi-user.target.wants
sudo ln -sfn /usr/lib/systemd/system/ssh.service \
  /tmp/ssh-rootfs-lower/etc/systemd/system/multi-user.target.wants/ssh.service
sudo sync
sudo mount -o remount,bind,ro /tmp/ssh-rootfs-lower
sudo mount -o remount,ro /
sudo umount /tmp/ssh-rootfs-lower
sudo rmdir /tmp/ssh-rootfs-lower
```

Always verify that `/` is read-only afterward:

```sh
findmnt -no SOURCE,FSTYPE,OPTIONS /
```

## Reboot validation

After reboot, do not run any manual SSH activation command before testing.
From another machine:

```sh
ssh phablet@192.168.1.120
```

Then collect:

```sh
systemctl is-enabled ssh.service
systemctl is-active ssh.service
systemctl status ssh.service --no-pager
journalctl -b -u ssh.service --no-pager
ss -ltnp | grep ':22'
```

A successful test requires an SSH connection without local intervention and a
boot journal showing `ssh.service` started before the manual login.

### Validation result

The repaired phone completed a new boot at `2026-07-24 09:01:31` with boot ID
`95badcb8-a312-4cfa-bb20-92223e6b45d9`. Systemd started `ssh.service`
automatically at `09:01:46`, listening on IPv4 and IPv6 port 22. The first
key-authenticated remote connection succeeded at `09:01:59`; no local SSH
activation command was run after reboot. The system partition was still
read-only after validation.

## Updates and limitations

This repair survives normal reboots because it modifies the current
`system_a` filesystem. A full Ubuntu Touch OTA can replace that system
partition and remove the lower-root symlink. After an OTA, test SSH before
depending on remote-only access and reapply the link if necessary.

The durable platform-level correction would be for Ubuntu Touch to pull a
packaged SSH-enablement unit into the early boot transaction after
`/etc/systemd/system` is mounted, or otherwise reload target dependencies after
the persistent bind mount becomes available. That should be fixed in the image
or `lxc-android-config`; it cannot be expressed solely as a custom unit inside
the late-mounted directory.

## Security

Keep public-key authentication. `PasswordAuthentication=no` matches UBports'
default and avoids exposing the device lock password to network login. The
authorized keys are stored persistently in
`/home/phablet/.ssh/authorized_keys`; directory and file modes should remain
`0700` and `0600`.

## Rollback

Rollback requires temporarily exposing and writing the lower root filesystem,
then removing only this link:

```sh
sudo mkdir -p /tmp/ssh-rootfs-lower
sudo mount --bind / /tmp/ssh-rootfs-lower
sudo mount -o remount,rw /
sudo mount -o remount,bind,rw /tmp/ssh-rootfs-lower
sudo rm -f /tmp/ssh-rootfs-lower/etc/systemd/system/multi-user.target.wants/ssh.service
sudo sync
sudo mount -o remount,bind,ro /tmp/ssh-rootfs-lower
sudo mount -o remount,ro /
sudo umount /tmp/ssh-rootfs-lower
sudo rmdir /tmp/ssh-rootfs-lower
```

Do not roll this back without local or ADB access.
