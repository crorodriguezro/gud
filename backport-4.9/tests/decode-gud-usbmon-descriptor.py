#!/usr/bin/env python3
"""Decode a served GUD display descriptor from a usbmon text capture."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

GUD_REQ_GET_DESCRIPTOR = 0x01
GUD_DISPLAY_MAGIC = 0x1D50614D
GUD_PROTOCOL_VERSION = 1
GUD_DISPLAY_FLAG_STATUS_ON_SET = 1 << 0
DESCRIPTOR = struct.Struct("<IBIBIIIII")
HEX_DATA = re.compile(r"=\s*((?:[0-9a-fA-F]{2}\s*)+)$")


class CaptureError(ValueError):
    """A capture cannot prove one usable served descriptor."""


@dataclass(frozen=True)
class Submission:
    tag: str
    ordinal: int


def parse_submission(tokens: list[str], ordinal: int) -> Submission | None:
    if len(tokens) < 8 or tokens[2] != "S" or not tokens[3].startswith("Ci:"):
        return None
    try:
        setup_index = tokens.index("s", 4)
        request_type = int(tokens[setup_index + 1], 16)
        request = int(tokens[setup_index + 2], 16)
    except (ValueError, IndexError):
        return None
    if request_type & 0x80 == 0 or request != GUD_REQ_GET_DESCRIPTOR:
        return None
    return Submission(tokens[0], ordinal)


def completion_payload(line: str, tag: str) -> bytes | None:
    tokens = line.split()
    if len(tokens) < 6 or tokens[0] != tag or tokens[2] != "C":
        return None
    if not tokens[3].startswith("Ci:"):
        return None
    try:
        status = int(tokens[4], 10)
        reported_length = int(tokens[5], 10)
    except ValueError as error:
        raise CaptureError(f"completion {tag} has invalid status/length") from error
    if status != 0:
        raise CaptureError(f"completion {tag} failed with status {status}")
    match = HEX_DATA.search(line)
    if not match:
        raise CaptureError(f"completion {tag} has no captured data")
    payload = bytes.fromhex(match.group(1))
    if reported_length < DESCRIPTOR.size or len(payload) < DESCRIPTOR.size:
        raise CaptureError(
            f"completion {tag} is truncated: reported={reported_length}, "
            f"captured={len(payload)}, required={DESCRIPTOR.size}"
        )
    return payload[: DESCRIPTOR.size]


def decode(payload: bytes) -> dict[str, int | str]:
    (
        magic,
        version,
        flags,
        compression,
        max_buffer_size,
        min_width,
        max_width,
        min_height,
        max_height,
    ) = DESCRIPTOR.unpack(payload)
    if magic != GUD_DISPLAY_MAGIC:
        raise CaptureError(f"wrong GUD magic 0x{magic:08x}")
    if version != GUD_PROTOCOL_VERSION:
        raise CaptureError(f"unsupported GUD protocol version {version}")
    if flags & GUD_DISPLAY_FLAG_STATUS_ON_SET:
        raise CaptureError(
            "served descriptor enables forbidden GUD_DISPLAY_FLAG_STATUS_ON_SET"
        )
    return {
        "magic": f"0x{magic:08x}",
        "version": version,
        "flags": flags,
        "compression": compression,
        "max_buffer_size": max_buffer_size,
        "min_width": min_width,
        "max_width": max_width,
        "min_height": min_height,
        "max_height": max_height,
    }


def select_descriptor(lines: list[str], occurrence: int | None) -> dict[str, int | str]:
    submissions: list[Submission] = []
    for line in lines:
        submission = parse_submission(line.split(), len(submissions) + 1)
        if submission is not None:
            submissions.append(submission)
    if not submissions:
        raise CaptureError("no GUD_REQ_GET_DESCRIPTOR control-IN submission found")
    if occurrence is not None:
        if occurrence < 1 or occurrence > len(submissions):
            raise CaptureError(
                f"descriptor occurrence {occurrence} is out of range "
                f"(found {len(submissions)})"
            )
        selected = [submissions[occurrence - 1]]
    else:
        selected = submissions

    decoded: list[tuple[int, dict[str, int | str]]] = []
    errors: list[str] = []
    for submission in selected:
        try:
            payload = next(
                payload
                for line in lines
                if (payload := completion_payload(line, submission.tag)) is not None
            )
            decoded.append((submission.ordinal, decode(payload)))
        except StopIteration:
            errors.append(f"occurrence {submission.ordinal} has no completion")
        except CaptureError as error:
            errors.append(f"occurrence {submission.ordinal}: {error}")

    if occurrence is not None:
        if errors:
            raise CaptureError(errors[0])
        return decoded[0][1]
    if len(decoded) != 1:
        detail = "; ".join(errors) if errors else "multiple usable descriptors"
        raise CaptureError(
            f"expected exactly one usable served descriptor, found {len(decoded)}"
            + (f": {detail}" if detail else "")
        )
    return decoded[0][1]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="decode a served GUD display descriptor from usbmon text"
    )
    parser.add_argument("capture", type=Path, help="snaplen-sufficient usbmon text capture")
    parser.add_argument(
        "--occurrence",
        type=int,
        help="select this 1-based GET_DESCRIPTOR submission when the capture has several",
    )
    args = parser.parse_args()
    try:
        result = select_descriptor(
            args.capture.read_text(encoding="utf-8").splitlines(), args.occurrence
        )
    except (OSError, UnicodeError, CaptureError) as error:
        parser.error(str(error))
    print(json.dumps(result, separators=(",", ":"), sort_keys=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
