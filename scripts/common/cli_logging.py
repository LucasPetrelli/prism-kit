"""Run CLI commands while teeing stdout and stderr to per-run log files."""

from __future__ import annotations

from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import threading
import traceback
from typing import Callable

_STREAM_DESCRIPTORS = (1, 2)
_READ_CHUNK_SIZE = 64 * 1024


def _project_root() -> Path:
    """Return the Prism Kit root from this module's package location."""
    return Path(__file__).resolve().parents[2]


def _flush_standard_streams() -> None:
    """Flush buffered Python streams before changing or restoring file descriptors."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.flush()
        except (OSError, ValueError):
            pass


def _write_all(descriptor: int, data: bytes) -> None:
    """Write all bytes to a descriptor, handling short writes."""
    pending = memoryview(data)
    while pending:
        written = os.write(descriptor, pending)
        if written <= 0:
            raise OSError("stream write returned no progress")
        pending = pending[written:]


class _OutputTee:
    """Capture process stdout and stderr while relaying each stream live."""

    def __init__(self, log_stream: object) -> None:
        self._log_stream = log_stream
        self._log_lock = threading.Lock()
        self._error_lock = threading.Lock()
        self.errors: list[OSError] = []
        self._saved_descriptors: dict[int, int] = {}
        self._redirected_descriptors: list[int] = []
        self._read_descriptors: list[int] = []
        self._write_descriptors: list[int] = []
        self._threads: list[threading.Thread] = []
        self._windows_api: tuple[object, dict[int, int | None]] | None = None
        self._redirected_windows_handles: list[int] = []

    def start(self) -> None:
        """Redirect stdout and stderr through reader threads into the log."""
        _flush_standard_streams()
        self._saved_descriptors = {
            descriptor: os.dup(descriptor) for descriptor in _STREAM_DESCRIPTORS
        }
        if os.name == "nt":
            self._save_windows_standard_handles()

        try:
            for descriptor in _STREAM_DESCRIPTORS:
                self._redirect_stream(descriptor)
        except BaseException:
            self.stop()
            raise

    def _save_windows_standard_handles(self) -> None:
        """Save Win32 standard handles because subprocesses use them directly."""
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetStdHandle.argtypes = [wintypes.DWORD]
        kernel32.GetStdHandle.restype = wintypes.HANDLE
        kernel32.SetStdHandle.argtypes = [wintypes.DWORD, wintypes.HANDLE]
        kernel32.SetStdHandle.restype = wintypes.BOOL
        handle_ids = {1: 0xFFFFFFF5, 2: 0xFFFFFFF4}
        saved_handles = {
            descriptor: kernel32.GetStdHandle(handle_id)
            for descriptor, handle_id in handle_ids.items()
        }
        self._windows_api = (kernel32, saved_handles)

    def _redirect_stream(self, descriptor: int) -> None:
        """Connect one process stream to a pipe and start its tee reader."""
        read_descriptor, write_descriptor = os.pipe()
        self._read_descriptors.append(read_descriptor)
        self._write_descriptors.append(write_descriptor)

        os.dup2(write_descriptor, descriptor, inheritable=True)
        self._redirected_descriptors.append(descriptor)
        os.close(write_descriptor)
        self._write_descriptors.remove(write_descriptor)

        if self._windows_api is not None:
            self._set_windows_standard_handle(descriptor)
            self._redirected_windows_handles.append(descriptor)

        thread = threading.Thread(
            target=self._relay_output,
            args=(read_descriptor, self._saved_descriptors[descriptor]),
            name=f"cli-log-stream-{descriptor}",
            daemon=True,
        )
        thread.start()
        self._threads.append(thread)
        self._read_descriptors.remove(read_descriptor)

    def _set_windows_standard_handle(self, descriptor: int) -> None:
        """Point the Win32 stdout/stderr handle at the redirected descriptor."""
        import msvcrt

        if self._windows_api is None:
            return
        kernel32, _ = self._windows_api
        handle_ids = {1: 0xFFFFFFF5, 2: 0xFFFFFFF4}
        handle = msvcrt.get_osfhandle(descriptor)
        if not kernel32.SetStdHandle(handle_ids[descriptor], handle):
            import ctypes

            raise ctypes.WinError(ctypes.get_last_error())

    def _restore_windows_standard_handle(self, descriptor: int) -> None:
        """Restore a saved Win32 standard handle."""
        if self._windows_api is None:
            return
        kernel32, saved_handles = self._windows_api
        handle_ids = {1: 0xFFFFFFF5, 2: 0xFFFFFFF4}
        if not kernel32.SetStdHandle(handle_ids[descriptor], saved_handles[descriptor]):
            import ctypes

            self._record_error(ctypes.WinError(ctypes.get_last_error()))

    def _record_error(self, error: OSError) -> None:
        """Retain capture errors without stopping the other output stream."""
        with self._error_lock:
            self.errors.append(error)

    def _relay_output(self, read_descriptor: int, console_descriptor: int) -> None:
        """Copy one pipe's bytes to its original console and the combined log."""
        try:
            while True:
                chunk = os.read(read_descriptor, _READ_CHUNK_SIZE)
                if not chunk:
                    return
                try:
                    _write_all(console_descriptor, chunk)
                except OSError as exc:
                    self._record_error(exc)
                try:
                    with self._log_lock:
                        self._write_log_chunk(chunk)
                except OSError as exc:
                    self._record_error(exc)
        except OSError as exc:
            self._record_error(exc)
        finally:
            os.close(read_descriptor)

    def _write_log_chunk(self, chunk: bytes) -> None:
        """Write a complete raw output chunk to the shared log stream."""
        pending = memoryview(chunk)
        while pending:
            written = self._log_stream.write(pending)
            if written is None or written <= 0:
                raise OSError("log write returned no progress")
            pending = pending[written:]

    def stop(self) -> None:
        """Restore process streams and wait until both pipes are fully drained."""
        _flush_standard_streams()
        for descriptor in reversed(self._redirected_descriptors):
            if descriptor in self._redirected_windows_handles:
                self._restore_windows_standard_handle(descriptor)
            try:
                os.dup2(self._saved_descriptors[descriptor], descriptor)
            except OSError as exc:
                self._record_error(exc)

        for descriptor in self._write_descriptors:
            try:
                os.close(descriptor)
            except OSError as exc:
                self._record_error(exc)
        self._write_descriptors.clear()

        for descriptor in self._read_descriptors:
            try:
                os.close(descriptor)
            except OSError as exc:
                self._record_error(exc)
        self._read_descriptors.clear()

        for thread in self._threads:
            thread.join()
        self._threads.clear()

        for descriptor in self._saved_descriptors.values():
            try:
                os.close(descriptor)
            except OSError as exc:
                self._record_error(exc)
        self._saved_descriptors.clear()
        self._redirected_descriptors.clear()
        self._redirected_windows_handles.clear()


def _exit_code_from_result(result: object) -> int:
    """Convert a CLI return value to the code returned by a console entrypoint."""
    if result is None:
        return 0
    if isinstance(result, int):
        return result
    return 1


def _exit_code_from_system_exit(error: SystemExit) -> int:
    """Convert ``SystemExit`` into a return code while preserving its message."""
    if error.code is None:
        return 0
    if isinstance(error.code, int):
        return error.code
    print(error.code, file=sys.stderr)
    return 1


def _publish_log(
    temporary_path: Path,
    log_directory: Path,
    timestamp: str,
    script_name: str,
    return_code: int,
) -> Path:
    """Publish the completed log without replacing an earlier same-second run."""
    base_name = f"{timestamp}-{script_name}-{return_code}"
    sequence = 0
    while True:
        suffix = "" if sequence == 0 else f"-{sequence:02d}"
        final_path = log_directory / f"{base_name}{suffix}.log"
        try:
            with final_path.open("xb") as final_stream:
                with temporary_path.open("rb") as temporary_stream:
                    shutil.copyfileobj(temporary_stream, final_stream)
        except FileExistsError:
            sequence += 1
            continue
        except BaseException:
            final_path.unlink(missing_ok=True)
            raise
        temporary_path.unlink(missing_ok=True)
        return final_path


def run_cli(
    script_name: str,
    entrypoint: Callable[[], int | None],
    *,
    project_root: Path | None = None,
) -> int:
    """Run one command and copy stdout/stderr to ``logs/*.log``.

    Args:
        script_name: Stable CLI name used in the log filename.
        entrypoint: Command function that returns its process exit code.
        project_root: Optional project root override for isolated tests.

    Returns:
        The command's process exit code.
    """
    safe_name = re.sub(r"[^A-Za-z0-9._-]+", "-", script_name).strip("-_.")
    safe_name = safe_name or "script"
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log_directory = (project_root or _project_root()) / "logs"
    log_directory.mkdir(parents=True, exist_ok=True)

    temporary_descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{timestamp}-{safe_name}-",
        suffix=".tmp",
        dir=log_directory,
    )
    temporary_path = Path(temporary_name)
    return_code = 1

    with os.fdopen(temporary_descriptor, "wb", buffering=0) as log_stream:
        tee = _OutputTee(log_stream)
        tee.start()
        try:
            return_code = _exit_code_from_result(entrypoint())
        except SystemExit as exc:
            return_code = _exit_code_from_system_exit(exc)
        except KeyboardInterrupt:
            traceback.print_exc()
            return_code = 130
        except Exception:
            traceback.print_exc()
            return_code = 1
        finally:
            tee.stop()

    published_path = _publish_log(
        temporary_path,
        log_directory,
        timestamp,
        safe_name,
        return_code,
    )
    if tee.errors:
        print(
            f"warning: output log may be incomplete ({tee.errors[0]}): {published_path}",
            file=sys.stderr,
        )
    return return_code
