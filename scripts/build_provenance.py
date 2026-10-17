"""Content-validated provenance for the latest successful project build."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import tempfile
from typing import Any

PROVENANCE_FILENAME = ".prism-kit-build-provenance.json"
PROVENANCE_SCHEMA_VERSION = 1
ARTIFACT_SUFFIXES = (".elf", ".hex", ".bin")
_REQUIRED_FIELDS = {
    "schema_version",
    "board_target",
    "artifact_path",
    "artifact_sha256",
    "config_sha256",
}
_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


class BuildProvenanceError(ValueError):
    """Raised when build provenance is missing, malformed, or stale."""


@dataclass(frozen=True)
class BuildProvenance:
    """Validated identity and files for one successful project build."""

    board_target: str
    artifact_path: Path
    artifact_sha256: str
    config_sha256: str


def build_config_path(root: Path) -> Path:
    """Return the Zephyr config path for *root*.

    Args:
        root: The Prism Kit project root.

    Returns:
        The generated Zephyr ``.config`` path.
    """
    return root / "build" / "zephyr" / ".config"


def provenance_path(root: Path) -> Path:
    """Return the build provenance manifest path for *root*.

    Args:
        root: The Prism Kit project root.

    Returns:
        The versioned manifest path under the generated build directory.
    """
    return root / "build" / PROVENANCE_FILENAME


def read_board_target(config_path: Path) -> str:
    """Read the fully qualified Zephyr board target from ``.config``.

    Args:
        config_path: Zephyr's generated Kconfig output.

    Returns:
        The value of ``CONFIG_BOARD_TARGET`` without Kconfig quotes.

    Raises:
        FileNotFoundError: If the config file does not exist.
        BuildProvenanceError: If the target is absent or empty.
    """
    if not config_path.is_file():
        raise FileNotFoundError(
            f"Zephyr config '{config_path}' is missing; build the project first."
        )

    expected_prefix = "CONFIG_BOARD_TARGET="
    for line in config_path.read_text(encoding="utf-8").splitlines():
        if not line.startswith(expected_prefix):
            continue

        target = line.split("=", 1)[1].strip()
        if target.startswith('"') and target.endswith('"'):
            target = target[1:-1]
        if target:
            return target
        break

    raise BuildProvenanceError(
        f"CONFIG_BOARD_TARGET was not found or is empty in '{config_path}'."
    )


def resolve_default_artifact(build_dir: Path) -> Path:
    """Resolve the artifact the no-argument flash path would select.

    Args:
        build_dir: Zephyr's generated output directory.

    Returns:
        The canonical artifact, or the newest supported artifact when no
        canonical filename exists.

    Raises:
        FileNotFoundError: If the build directory or a supported artifact is
            missing.
    """
    if not build_dir.is_dir():
        raise FileNotFoundError(
            f"Build directory '{build_dir}' was not found. Build the firmware first."
        )

    for suffix in ARTIFACT_SUFFIXES:
        canonical_candidate = build_dir / f"zephyr{suffix}"
        if canonical_candidate.is_file():
            return canonical_candidate.resolve()

        candidates = [path for path in build_dir.glob(f"*{suffix}") if path.is_file()]
        if candidates:
            return max(candidates, key=lambda path: path.stat().st_mtime).resolve()

    raise FileNotFoundError(
        f"No supported artifacts were found under '{build_dir}'. Build the firmware first."
    )


def sha256_file(path: Path) -> str:
    """Calculate a file's SHA-256 digest using bounded memory.

    Args:
        path: File to fingerprint.

    Returns:
        Lower-case hexadecimal SHA-256 digest.
    """
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_build_provenance(
    root: Path,
    expected_board_target: str,
    artifact: Path | None = None,
) -> BuildProvenance:
    """Atomically publish provenance after a successful build pipeline.

    Args:
        root: The Prism Kit project root.
        expected_board_target: Fully qualified target selected by the build
            wrapper.
        artifact: The default flash artifact; resolved from the build directory
            when omitted.

    Returns:
        The values written to the provenance manifest.

    Raises:
        FileNotFoundError: If the config or artifact is missing.
        BuildProvenanceError: If the config target or artifact path does not
            match the selected build.
        OSError: If the manifest cannot be written atomically.
    """
    project_root = root.resolve()
    config_path = build_config_path(project_root)
    actual_board_target = read_board_target(config_path)
    if actual_board_target != expected_board_target:
        raise BuildProvenanceError(
            "Build target mismatch: selected target "
            f"'{expected_board_target}', but '{config_path}' reports "
            f"'{actual_board_target}'."
        )

    build_dir = project_root / "build" / "zephyr"
    selected_artifact = (
        resolve_default_artifact(build_dir) if artifact is None else artifact.resolve()
    )
    if not selected_artifact.is_file():
        raise FileNotFoundError(f"Build artifact '{selected_artifact}' is missing.")
    if selected_artifact.suffix.lower() not in ARTIFACT_SUFFIXES:
        raise BuildProvenanceError(f"Unsupported build artifact '{selected_artifact}'.")
    try:
        relative_artifact = selected_artifact.relative_to(build_dir.resolve())
        artifact_path = selected_artifact.relative_to(project_root).as_posix()
    except ValueError as exc:
        raise BuildProvenanceError(
            f"Build artifact '{selected_artifact}' is outside '{build_dir}'."
        ) from exc
    if not relative_artifact.parts:
        raise BuildProvenanceError("The default build artifact path is invalid.")

    artifact_sha256 = sha256_file(selected_artifact)
    config_sha256 = sha256_file(config_path)
    manifest = {
        "schema_version": PROVENANCE_SCHEMA_VERSION,
        "board_target": actual_board_target,
        "artifact_path": artifact_path,
        "artifact_sha256": artifact_sha256,
        "config_sha256": config_sha256,
    }

    manifest_file = provenance_path(project_root)
    manifest_file.parent.mkdir(parents=True, exist_ok=True)
    temporary_file: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            newline="\n",
            prefix=f".{manifest_file.name}.",
            suffix=".tmp",
            dir=manifest_file.parent,
            delete=False,
        ) as handle:
            temporary_file = Path(handle.name)
            json.dump(manifest, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_file, manifest_file)
    finally:
        if temporary_file is not None:
            temporary_file.unlink(missing_ok=True)

    return BuildProvenance(
        board_target=actual_board_target,
        artifact_path=selected_artifact,
        artifact_sha256=artifact_sha256,
        config_sha256=config_sha256,
    )


def load_build_provenance(root: Path) -> BuildProvenance:
    """Load and validate the manifest and its fingerprinted build files.

    Args:
        root: The Prism Kit project root.

    Returns:
        Validated board identity and absolute artifact path.

    Raises:
        FileNotFoundError: If the manifest, config, or artifact is missing.
        BuildProvenanceError: If the manifest is malformed, unsupported, or
            does not match the current config and artifact contents.
    """
    project_root = root.resolve()
    manifest_file = provenance_path(project_root)
    if not manifest_file.is_file():
        raise FileNotFoundError(
            f"Build provenance '{manifest_file}' is missing. Run a successful "
            "project build before flashing without an artifact override."
        )

    try:
        manifest: Any = json.loads(manifest_file.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' is malformed. Rebuild the project."
        ) from exc

    if not isinstance(manifest, dict) or set(manifest) != _REQUIRED_FIELDS:
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' has an invalid schema. Rebuild the project."
        )

    schema_version = manifest["schema_version"]
    if type(schema_version) is not int or schema_version != PROVENANCE_SCHEMA_VERSION:
        raise BuildProvenanceError(
            f"Unsupported build provenance schema version '{schema_version}'."
        )

    board_target = manifest["board_target"]
    if not isinstance(board_target, str) or not board_target.strip():
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' has an invalid board target."
        )

    artifact_path_text = manifest["artifact_path"]
    if not isinstance(artifact_path_text, str) or not artifact_path_text:
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' has an invalid artifact path."
        )
    relative_artifact = Path(artifact_path_text)
    if (
        relative_artifact.is_absolute()
        or PureWindowsPath(artifact_path_text).is_absolute()
        or relative_artifact.as_posix() != artifact_path_text
    ):
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' must contain a project-relative artifact path."
        )

    artifact_path = (project_root / relative_artifact).resolve()
    build_dir = (project_root / "build" / "zephyr").resolve()
    try:
        artifact_path.relative_to(build_dir)
    except ValueError as exc:
        raise BuildProvenanceError(
            f"Recorded artifact '{artifact_path_text}' is outside the project build output."
        ) from exc
    if artifact_path.suffix.lower() not in ARTIFACT_SUFFIXES:
        raise BuildProvenanceError(
            f"Recorded artifact '{artifact_path_text}' has an unsupported file type."
        )
    if not artifact_path.is_file():
        raise FileNotFoundError(
            f"Recorded build artifact '{artifact_path}' is missing. Rebuild the project."
        )

    artifact_sha256 = manifest["artifact_sha256"]
    config_sha256 = manifest["config_sha256"]
    if not isinstance(artifact_sha256, str) or not _SHA256_PATTERN.fullmatch(
        artifact_sha256
    ):
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' has an invalid artifact SHA-256 value."
        )
    if not isinstance(config_sha256, str) or not _SHA256_PATTERN.fullmatch(
        config_sha256
    ):
        raise BuildProvenanceError(
            f"Build provenance '{manifest_file}' has an invalid config SHA-256 value."
        )

    config_path = build_config_path(project_root)
    current_board_target = read_board_target(config_path)
    if current_board_target != board_target:
        raise BuildProvenanceError(
            "Build provenance target mismatch: the manifest records "
            f"'{board_target}', but '{config_path}' reports '{current_board_target}'."
        )

    if sha256_file(artifact_path) != artifact_sha256:
        raise BuildProvenanceError(
            f"Recorded artifact '{artifact_path}' is stale; run a successful project build."
        )
    if sha256_file(config_path) != config_sha256:
        raise BuildProvenanceError(
            f"Zephyr config '{config_path}' is stale; run a successful project build."
        )

    return BuildProvenance(
        board_target=board_target,
        artifact_path=artifact_path,
        artifact_sha256=artifact_sha256,
        config_sha256=config_sha256,
    )
