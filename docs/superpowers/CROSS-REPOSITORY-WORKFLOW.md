# Cross-Repository Specification and Delivery Workflow

## Purpose

This workflow applies when a change or investigation affects more than one of
these repositories:

- `gud`: OnePlus Linux 4.9 GUD host driver and integration coordinator;
- `gud-gadget`: Raspberry Pi FunctionFS gadget and HDMI presentation; and
- `mir-android2-platform-gud`: experimental Mir Android-HWC external-output
  platform work.

It keeps the technical plan in one authoritative place while preserving the
operator instructions, source-level detail, and evidence with the component
that owns them.

## Source of truth and artifact placement

`gud` is the coordination repository for the current OnePlus/Pi project.
`gud/PROJECT-STATUS.md` is the short canonical priority board. It records the
shared ID, owner(s), lifecycle state, dependency, and exact acceptance evidence
for every cross-repository item.

| Artifact | Location | Use |
| --- | --- | --- |
| Cross-repository specification | `gud/docs/superpowers/specs/YYYY-MM-DD-<id>-<topic>-design.md` | Problem statement, boundaries, interfaces, invariants, ownership, and acceptance criteria. |
| Cross-repository plan | `gud/docs/superpowers/plans/YYYY-MM-DD-<id>-<topic>.md` | Ordered implementation/investigation tasks, dependencies, verification, and stop conditions. |
| Priority summary | `gud/PROJECT-STATUS.md` and, where relevant, `gud/BACKLOG.md` | One-line status, owners, and a link to the formal documents. |
| Component runbook/test procedure | Owning repo `docs/` | Exact commands, reset matrix, log collection, and evidence table needed to operate or test that component. |
| Incident record | Owning repo `docs/` | Symptoms, confirmed cause, repair, affected versions, and links to raw evidence. |
| POC record | Experimental repo root or `docs/` | Scope, known shortcuts, deployment/rollback state, demonstrated behavior, and reasons it is not production-ready. |

Do not duplicate a formal spec or plan in all repositories. Link to the
canonical document using a repository-relative path and keep only
component-specific executable instructions locally.

## When to create each artifact

Create a **specification** before implementation when work changes an
architecture boundary, crosses repositories, changes a hardware-test contract,
or has a realistic risk of appearing successful while failing the actual user
goal. A spec is not needed for a contained typo, isolated refactor, or a
well-understood one-file fix.

Create an **implementation plan** when the work has multiple dependent steps,
requires a hardware test sequence, needs a rollback boundary, or will be handed
between people/sessions. The plan must name its stop conditions; it must not
silently expand scope after a failed experiment.

Create or update a **component runbook** whenever an operator needs specific
commands, reset ordering, device discovery, or log capture to run the work.
The runbook is the executable truth; the plan should point to it rather than
repeating commands that will drift.

Create a **POC record** before or with the first experimental commit. It must
say what the POC proves, what it does not prove, whether it is deployed, and
how it is rolled back. A visible frame or a successfully enumerated device is
never, by itself, a completion claim.

## Lifecycle

1. Choose the coordination repository and create a stable shared ID, for
   example `XDISP-P0.1`.
2. Add the board entry in `gud/PROJECT-STATUS.md`: owner(s), state, dependency,
   and measurable acceptance criterion.
3. Add the Superpowers spec. For nontrivial work, add its paired plan before
   modifying implementation code.
4. Add or update the owning component's runbook/test procedure and link it to
   the formal spec and plan.
5. Implement in small, repository-local commits. A commit message or issue
   reference should include the shared ID when it advances that item.
6. Collect the acceptance evidence specified up front. Retain raw hardware
   logs outside version control unless a selected evidence artifact is useful
   and safe to version.
7. Update the board state only from evidence, then update the incident/POC
   record with the observed result and remaining limits.

## States and evidence rules

Use only these states on the shared board:

- **planned** — scoped, not started;
- **in progress** — a documented hypothesis or implementation is actively
  being tested;
- **blocked** — a named dependency, reproducible failure, or missing evidence
  prevents progress;
- **verified** — the stated acceptance criteria and retained evidence pass; and
- **rolled back** — an experiment has useful recorded results but is disabled
  from the normal system.

For hardware work, a state changes to **verified** only when the exact test
matrix passes and the required host/device logs exist. Compilation, USB
enumeration, a single frame, or a later retry succeeding are diagnostic data,
not verification unless the specification explicitly says otherwise.

## Cross-repository commit rules

- Keep commits repository-local and focused; do not combine host driver,
  gadget, and Mir changes in one commit merely because they support one goal.
- Commit an experimental POC on a clearly named branch with an accompanying
  POC record. Do not merge or deploy it as normal production behavior until its
  acceptance item is verified.
- Keep unverified experiments separate from diagnostic documentation whenever
  practical. Do not use a documentation commit to imply an experimental code
  change is validated.
- Preserve unrelated working-tree changes and untracked hardware artifacts.
- A cross-repository completion requires compatible commits plus the board and
  evidence update; it is not completed when only one repository is committed.

## Minimal template

Use this minimum information for a new board entry:

| ID | Priority | Owner | State | Dependency | Acceptance evidence |
| --- | --- | --- | --- | --- | --- |
| `AREA-Px.n` | Concise outcome | repo(s) | planned | named gate or none | exact test and retained logs |

The matching spec must answer: what failed, what is in/out of scope, what
interfaces cross repositories, which invariants must hold, and exactly what
evidence permits the state to become **verified**. The matching plan must name
the next executable action and the condition that stops or redirects it.

## Current example

`XDISP-P0.1` follows this workflow:

- board: `PROJECT-STATUS.md`;
- spec: `specs/2026-07-25-xdisp-p0-1-functionfs-rebind-design.md`;
- plan: `plans/2026-07-25-xdisp-p0-1-functionfs-rebind.md`; and
- Pi procedure: `../../../gud-gadget/docs/XDISP-P0.1-FUNCTIONFS-REBIND-TEST.md`.

Its POC consumer, `mir-android2-platform-gud`, remains rolled back until the
Pi transport gate is verified and the asynchronous presentation work has its
own spec and plan.
