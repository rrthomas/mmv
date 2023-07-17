import os
import sys
import subprocess
import difflib
from contextlib import ExitStack
from pathlib import Path
from dataclasses import dataclass
from unittest.mock import patch
from typing import Any, Callable, List, Optional, Iterator

import pytest
from pytest import CaptureFixture, mark, param


if sys.version_info[:2] >= (3, 11):
    from contextlib import chdir
else:
    from contextlib import contextmanager

    @contextmanager
    def chdir(path: os.PathLike[str]) -> Iterator[None]:
        old_dir = os.getcwd()
        os.chdir(path)
        try:
            yield
        finally:
            os.chdir(old_dir)


@dataclass
class Case:
    name: str
    args: List[str]
    input: Path
    output: Path
    error: Optional[int] = None


def compare_text_files(
    capsys: CaptureFixture[str],
    output_file: os.PathLike[str],
    expected_file: os.PathLike[str],
) -> bool:
    with ExitStack() as stack:
        out_fd = stack.enter_context(open(output_file, encoding="ascii"))
        exp_fd = stack.enter_context(open(expected_file, encoding="ascii"))
        output_lines = out_fd.readlines()
        expected_lines = exp_fd.readlines()
        diff = list(
            difflib.unified_diff(
                output_lines, expected_lines, str(output_file), str(expected_file)
            )
        )
        if len(diff) > 0:
            with capsys.disabled():
                sys.stdout.writelines(diff)
        return len(diff) == 0
    return False


def compare_strings(
    capsys: CaptureFixture[str],
    output: str,
    output_file: os.PathLike[str],
    expected_file: os.PathLike[str],
) -> bool:
    with open(output_file, "w", encoding="ascii") as f:
        f.write(output)
    return compare_text_files(capsys, output_file, expected_file)


def list_directory(directory: Path) -> None:
    subprocess.check_output(
        [
            "find",
            str(directory),
            "-type",
            "d",
            "-printf",
            "%P %U:%G %M\n",
            "-or",
            "-printf",
            "%P %U:%G %M %s\n",
        ],
        text=True,
        stderr=subprocess.STDOUT,
    )


def get_directory_info(directory: Path) -> List[str]:
    return subprocess.check_output(
        [
            "find",
            directory,
            "-type",
            "d",
            "-printf",
            r"%P %U:%G %M\n",
            "-or",
            "-printf",
            r"%P %U:%G %M %s\n",
        ],
        text=True,
    ).split("\n")


def compare_directories(
    capsys: CaptureFixture[str], actual: Path, expected: Path
) -> bool:
    actual_info = get_directory_info(actual)
    expected_info = get_directory_info(expected)
    diff = list(
        difflib.unified_diff(actual_info, expected_info, str(actual), str(expected))
    )
    if len(diff) > 0:
        with capsys.disabled():
            sys.stdout.writelines(diff)
    return len(diff) == 0


def directory_test(
    function: Callable[[List[str]], None],
    case: Case,
    fixture_dir: Path,
    capsys: CaptureFixture[str],
    tmp_path: Path,
    regenerate_expected: bool,
) -> None:
    module_name = function.__name__
    expected_stderr = fixture_dir / module_name / case.name / "expected-stdouterr.txt"
    patched_argv = [module_name, *(sys.argv[1:])]
    with chdir(tmp_path):
        correct_output = True
        if case.error is None:
            with patch("sys.argv", patched_argv):
                function(case.args)
            if regenerate_expected:
                subprocess.run([], check=True)  # FIXME: run mmv
            else:
                correct_output = compare_directories(capsys, case.input, case.output)
        else:
            with pytest.raises(SystemExit) as e:
                with patch("sys.argv", patched_argv):
                    function(case.args)
            assert e.type == SystemExit
            assert e.value.code == case.error
        if regenerate_expected:
            with open(expected_stderr, "w", encoding="utf-8") as f:
                f.write(capsys.readouterr().err)
        else:
            correct_output = (
                compare_strings(
                    capsys,
                    capsys.readouterr().err,
                    tmp_path / "stderr.txt",
                    expected_stderr,
                )
                and correct_output
            )
        if not correct_output:
            raise ValueError("test output does not match expected output")


def make_tests(
    function: Callable[..., Any],
    fixture_dir: Path,
    *tests: Case,
) -> Any:
    ids = []
    test_cases = []
    for t in tests:
        ids.append(t.name)
        test_cases.append(t)
    return mark.parametrize(
        "function,case,fixture_dir",
        [
            param(
                function,
                case,
                fixture_dir,
                marks=mark.datafiles,
            )
            for case in test_cases
        ],
        ids=ids,
    )
