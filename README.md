# GUD

Upstream project wiki: https://github.com/notro/gud/wiki

## Linux 4.9 backport project

This branch tracks an experimental backport of the host-side Generic USB Display DRM driver to Linux 4.9, initially targeting the OnePlus 6 Ubuntu Touch / Halium kernel.

Start here:

- [`LINUX-4.9-BACKPORT.md`](LINUX-4.9-BACKPORT.md) — architecture, scope, milestones, and testing strategy.
- [`BACKLOG.md`](BACKLOG.md) — ordered implementation backlog.
- [`AGENTS.md`](AGENTS.md) — Codex/development instructions and constraints.

Primary goal: produce a standalone `gud.ko` that can be loaded into the existing OnePlus 6 Ubuntu Touch kernel and expose a GUD adapter as a DRM/KMS external display without flashing a replacement kernel.
