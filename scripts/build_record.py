"""Immutable linked-footprint records for pristine Prism Kit commits."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from typing import Sequence

if __package__:
    from .build_provenance import (
        build_config_path,
        resolve_default_artifact,
        sha256_file,
    )
else:
    from build_provenance import (
        build_config_path,
        resolve_default_artifact,
        sha256_file,
    )

RECORD_DIRECTORY = ".prism-kit-build-records"
RECORD_SCHEMA_VERSION = 1
MEMORY_REGIONS = ("FLASH", "RAM")
REPORT_REGIONS = ("ROM", "RAM")
_MEMORY_USAGE_PATTERN = re.compile(
    r"^\s*(FLASH|RAM):\s*([\d,]+)\s+B(?:\s|$)", re.MULTILINE
)

_MAP_REGION_PATTERN = re.compile(
    r"^\s*(FLASH|RAM)\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)(?:\s|$)",
    re.MULTILINE,
)


class BuildRecordError(ValueError):
    """Raised when successful-build outputs cannot produce a valid record."""


@dataclass(frozen=True)
class BuildRecordResult:
    """Result of the optional immutable build-record publication."""

    path: Path | None
    created: bool
    reason: str | None = None


def parse_linker_usage(build_output: str) -> dict[str, int] | None:
    """Return GNU ld FLASH/RAM totals, or ``None`` if this run did not link.

    Args:
        build_output: Combined output from the project build wrapper's linker.

    Returns:
        The last reported byte total for each region, when both are present.
    """
    usage: dict[str, int] = {}
    for match in _MEMORY_USAGE_PATTERN.finditer(build_output):
        usage[match.group(1)] = int(match.group(2).replace(",", ""))
    if any(region not in usage for region in MEMORY_REGIONS):
        return None
    return usage


def parse_map_capacities(map_text: str) -> dict[str, int]:
    """Parse FLASH and RAM capacities from GNU ld's Memory Configuration.

    Args:
        map_text: Contents of the linked image's GNU ld map file.

    Returns:
        The configured capacity in bytes for each required region.

    Raises:
        BuildRecordError: If either required capacity cannot be found.
    """
    memory_config = map_text.split("Memory Configuration", maxsplit=1)
    if len(memory_config) != 2:
        raise BuildRecordError("The linker map has no Memory Configuration section.")

    capacities = {
        match.group(1): int(match.group(2), 16)
        for match in _MAP_REGION_PATTERN.finditer(memory_config[1])
    }
    missing = [region for region in MEMORY_REGIONS if region not in capacities]
    if missing:
        raise BuildRecordError(
            f"The linker map is missing capacities for: {', '.join(missing)}."
        )
    return capacities


def _run_git(root: Path, *arguments: str) -> str:
    """Run a read-only Git query and return its stripped standard output."""
    result = subprocess.run(
        ["git", *arguments],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
        errors="replace",
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise BuildRecordError(f"Git query failed ({' '.join(arguments)}): {detail}")
    return result.stdout.strip()


def _submodule_paths(root: Path) -> list[str]:
    """Read direct submodule paths without invoking Git's shell helper."""
    if not (root / ".gitmodules").is_file():
        return []
    result = subprocess.run(
        [
            "git",
            "config",
            "--file",
            ".gitmodules",
            "--get-regexp",
            r"^submodule\..*\.path$",
        ],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
        errors="replace",
    )
    if result.returncode not in (0, 1):
        detail = result.stderr.strip() or result.stdout.strip()
        raise BuildRecordError(f"Unable to read submodule paths: {detail}")
    return [line.split(maxsplit=1)[1] for line in result.stdout.splitlines() if line]


def _submodules_are_clean(root: Path) -> bool:
    """Verify initialized submodules match their gitlinks and have no changes."""
    for submodule_path in _submodule_paths(root):
        expected_entry = _run_git(root, "ls-tree", "HEAD", "--", submodule_path)
        if not expected_entry:
            return False
        entry_parts = expected_entry.split()
        if len(entry_parts) < 3 or entry_parts[1] != "commit":
            return False
        expected_revision = entry_parts[2]
        submodule_root = root / submodule_path
        if not (submodule_root / ".git").exists():
            return False
        actual_revision = _run_git(submodule_root, "rev-parse", "--verify", "HEAD")
        if actual_revision != expected_revision:
            return False
        status = _run_git(
            submodule_root,
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
            "--ignore-submodules=none",
        )
        if status or not _submodules_are_clean(submodule_root):
            return False
    return True


def pristine_revision(root: Path) -> tuple[str | None, str | None]:
    """Return HEAD only when the checkout and initialized submodules are clean.

    Ignored generated build outputs and user-local settings do not make the
    checkout dirty. Tracked, staged, non-ignored untracked, or modified
    submodule content prevents publishing a commit-keyed record.

    Args:
        root: The Prism Kit worktree root.

    Returns:
        ``(commit, None)`` for a pristine source tree, or ``(None, reason)``.
    """
    try:
        revision = _run_git(root, "rev-parse", "--verify", "HEAD")
        status = _run_git(
            root,
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
            "--ignore-submodules=none",
        )
    except (OSError, BuildRecordError) as exc:
        return None, str(exc)

    if status:
        return None, "the source worktree has staged, modified, or untracked files"

    try:
        if not _submodules_are_clean(root):
            return None, "a required submodule is uninitialized or modified"
    except (OSError, BuildRecordError) as exc:
        return None, str(exc)

    return revision, None


def shared_checkout_root(root: Path) -> Path:
    """Find the primary checkout root shared by linked Git worktrees.

    Args:
        root: Any worktree belonging to the Prism Kit repository.

    Returns:
        The parent of Git's common directory, used to retain records after
        temporary linked worktrees are removed.
    """
    common_dir_text = _run_git(root, "rev-parse", "--git-common-dir")
    common_dir = Path(common_dir_text)
    if not common_dir.is_absolute():
        common_dir = root / common_dir
    return common_dir.resolve().parent


def _record_path(
    root: Path,
    revision: str,
    board_alias: str,
    config_sha256: str,
    toolchain: dict[str, object],
) -> Path:
    """Build a stable file path for a source/config/toolchain identity."""
    binary_identity = {
        name: {key: values.get(key) for key in ("path", "sha256")}
        for name, values in toolchain.items()
        if isinstance(values, dict)
    }
    identity = json.dumps(
        {
            "board": board_alias,
            "config": config_sha256,
            "toolchain": binary_identity,
        },
        sort_keys=True,
    )
    identity_digest = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:12]
    directory = shared_checkout_root(root) / RECORD_DIRECTORY
    return directory / f"{revision}-{board_alias}-{identity_digest}.json"


def existing_build_record_path(
    root: Path,
    revision: str,
    board_alias: str,
    config_sha256: str,
    toolchain: dict[str, object],
) -> Path:
    """Return the immutable record path for one commit/build identity."""
    return _record_path(
        root,
        revision,
        board_alias,
        config_sha256,
        toolchain,
    )


def read_report_usage(root: Path) -> dict[str, int]:
    """Read byte totals from Zephyr's official ROM and RAM reports.

    Args:
        root: The Prism Kit worktree that produced the reports.

    Returns:
        ``ROM`` and ``RAM`` sizes in bytes from ``rom.json`` and ``ram.json``.

    Raises:
        BuildRecordError: If a report is absent, malformed, or lacks its total.
    """
    usage: dict[str, int] = {}
    for region, report_name in (("ROM", "rom.json"), ("RAM", "ram.json")):
        report_path = root / "build" / report_name
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise BuildRecordError(
                f"Unable to read Zephyr footprint report '{report_path}': {exc}"
            ) from exc
        total_size = report.get("total_size") if isinstance(report, dict) else None
        if not isinstance(total_size, int) or total_size < 0:
            raise BuildRecordError(
                f"Zephyr footprint report '{report_path}' has no valid total_size."
            )
        usage[region] = total_size
    return usage


def write_build_record(
    root: Path,
    *,
    expected_revision: str | None,
    board_alias: str,
    board_target: str,
    build_output: str,
    usage: dict[str, int],
    toolchain: dict[str, object],
    command: Sequence[str],
) -> BuildRecordResult:
    """Create a gitignored per-commit record after a successful clean build.

    Existing records are immutable and are never overwritten. The file is
    stored beside the repository's common Git directory so builds from
    temporary linked worktrees share it and cleanup preserves it.

    Args:
        root: The Prism Kit worktree that produced the build outputs.
        expected_revision: Clean HEAD captured immediately before the build,
            or ``None`` when the source was already dirty.
        board_alias: Project board alias used by the build wrapper.
        board_target: Fully qualified Zephyr target selected for the build.
        build_output: Combined output from the successful project build.
        usage: Linked ROM/RAM totals read from Zephyr's successful reports.
        toolchain: Compiler/linker paths, binary hashes, and known versions.
        command: Arguments supplied to the project build wrapper.

    Returns:
        Whether a new record was created, an existing record was retained, or
        the source was ineligible for commit-keyed recording.

    Raises:
        BuildRecordError: If the successful build outputs are incomplete.
        OSError: If the record cannot be written.
    """
    if expected_revision is None:
        return BuildRecordResult(
            path=None,
            created=False,
            reason="the source worktree was not pristine when the build started",
        )

    current_revision, dirty_reason = pristine_revision(root)
    if current_revision != expected_revision:
        reason = dirty_reason or "the source revision changed during the build"
        return BuildRecordResult(path=None, created=False, reason=reason)

    config_path = build_config_path(root)
    map_path = root / "build" / "zephyr" / "zephyr.map"
    if not map_path.is_file():
        raise BuildRecordError(f"Linker map '{map_path}' is missing.")

    capacities = parse_map_capacities(
        map_path.read_text(encoding="utf-8", errors="replace")
    )
    missing_usage = [region for region in REPORT_REGIONS if region not in usage]
    invalid_usage = [
        region
        for region in REPORT_REGIONS
        if region in usage
        and (
            usage[region] < 0
            or usage[region] > capacities["FLASH" if region == "ROM" else "RAM"]
        )
    ]
    if missing_usage or invalid_usage:
        raise BuildRecordError(
            "Linked usage is missing or invalid for: "
            f"{', '.join(missing_usage + invalid_usage)}."
        )
    config_sha256 = sha256_file(config_path)
    artifact_path = resolve_default_artifact(root / "build" / "zephyr")
    linker_usage = parse_linker_usage(build_output)
    record_path = _record_path(
        root,
        expected_revision,
        board_alias,
        config_sha256,
        toolchain,
    )

    record = {
        "schema_version": RECORD_SCHEMA_VERSION,
        "source": {
            "commit_sha1": expected_revision,
            "pristine_worktree": True,
        },
        "build": {
            "board_alias": board_alias,
            "board_target": board_target,
            "config_sha256": config_sha256,
            "map_sha256": sha256_file(map_path),
            "artifact_sha256": sha256_file(artifact_path),
            "rom_report_sha256": sha256_file(root / "build" / "rom.json"),
            "ram_report_sha256": sha256_file(root / "build" / "ram.json"),
            "toolchain": toolchain,
            "metric_source": "Zephyr size_report rom.json and ram.json total_size",
            "command": list(command),
        },
        "resources": {
            "linker_regions": {
                region: {
                    "used_bytes": (linker_usage or {}).get(region),
                    "capacity_bytes": capacities[region],
                }
                for region in MEMORY_REGIONS
            },
            "zephyr_reports": {
                "ROM": {
                    "used_bytes": usage["ROM"],
                    "capacity_bytes": capacities["FLASH"],
                    "capacity_region": "FLASH",
                },
                "RAM": {
                    "used_bytes": usage["RAM"],
                    "capacity_bytes": capacities["RAM"],
                    "capacity_region": "RAM",
                },
            },
        },
        "recorded_utc": datetime.now(timezone.utc).isoformat(),
    }

    record_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        handle = record_path.open("x", encoding="utf-8", newline="\n")
    except FileExistsError:
        return BuildRecordResult(path=record_path, created=False)

    try:
        with handle:
            json.dump(record, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
    except OSError:
        record_path.unlink(missing_ok=True)
        raise

    return BuildRecordResult(path=record_path, created=True)


def collect_toolchain_identity(
    root: Path,
    toolchain_root: Path,
    build_output: str,
) -> dict[str, object]:
    """Fingerprint compiler/linker binaries and extract versions when present.

    Args:
        root: The Prism Kit worktree containing the successful build.
        toolchain_root: Configured GNU Arm Embedded toolchain root.
        build_output: CMake/compiler output from the successful build.

    Returns:
        Version hints and SHA-256 identities for compiler and linker binaries.

    Raises:
        BuildRecordError: If either compiler or linker binary is unavailable.
    """
    compiler = toolchain_root / "bin" / "arm-none-eabi-gcc.exe"
    linker = resolve_linker_executable(toolchain_root)
    if not compiler.is_file():
        raise BuildRecordError(f"Compiler '{compiler}' is missing.")

    compiler_version_match = re.search(
        r"The C(?:XX)? compiler identification is GNU ([0-9.]+)", build_output
    )
    linker_version_match = re.search(
        r'Found GnuLd: .*?\(found version "([^"]+)"\)', build_output
    )
    if compiler_version_match is None:
        cache_path = root / "build" / "CMakeCache.txt"
        if cache_path.is_file():
            cache_text = cache_path.read_text(encoding="utf-8", errors="replace")
            compiler_version_match = re.search(
                r"^CMAKE_C_COMPILER_VERSION:INTERNAL=([^\r\n]+)",
                cache_text,
                re.MULTILINE,
            )

    return {
        "compiler": {
            "path": str(compiler.resolve()),
            "sha256": sha256_file(compiler),
            "version": (
                f"GNU {compiler_version_match.group(1)}"
                if compiler_version_match
                else None
            ),
        },
        "linker": {
            "path": str(linker.resolve()),
            "sha256": sha256_file(linker),
            "version": (
                f"GNU ld {linker_version_match.group(1)}"
                if linker_version_match
                else None
            ),
        },
    }


def resolve_linker_executable(toolchain_root: Path) -> Path:
    """Find the GNU ld executable inside the configured GNU Arm toolchain."""
    candidates = (
        toolchain_root / "bin" / "arm-none-eabi-ld.exe",
        toolchain_root / "arm-none-eabi" / "bin" / "ld.exe",
        toolchain_root / "arm-none-eabi" / "bin" / "ld.bfd.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise BuildRecordError(
        f"GNU ld was not found under the configured toolchain '{toolchain_root}'."
    )
