# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

from __future__ import annotations

import importlib.metadata
import argparse
import os
import sys
import re
import glob
import shutil
import tempfile
import warnings
from enum import Enum, auto
from dataclasses import dataclass
from warnings import warn
from pathlib import Path
from types import TracebackType
from typing import List, Optional, Union, Tuple, Type, NoReturn, TextIO
from typing_extensions import Self

VERSION = importlib.metadata.version("rpl")

PROGRAM_NAME: str


def simple_warning(  # pylint: disable=too-many-arguments
    message: Union[Warning, str],
    category: Type[Warning],  # pylint: disable=unused-argument
    filename: str,  # pylint: disable=unused-argument
    lineno: int,  # pylint: disable=unused-argument
    file: Optional[TextIO] = sys.stderr,
    line: Optional[str] = None,  # pylint: disable=unused-argument
) -> None:
    print(f"{PROGRAM_NAME}: {message}", file=file or sys.stderr)


warnings.showwarning = simple_warning


def die(code: int, msg: str) -> NoReturn:
    warn(Warning(msg))
    sys.exit(code)


# Help output
# Adapted from https://stackoverflow.com/questions/23936145/
class HelpFormatter(argparse.RawTextHelpFormatter):
    def _format_action_invocation(self, action: argparse.Action) -> str:
        if not action.option_strings:
            (metavar,) = self._metavar_formatter(action, action.dest)(1)
            return metavar
        parts: List[str] = []
        if action.nargs == 0:
            # Option takes no argument, output: -s, --long
            parts.extend(action.option_strings)
        else:
            # Option takes an argument, output: -s, --long ARGUMENT
            default = action.dest.upper()
            args_string = self._format_args(action, default)
            for option_string in action.option_strings:
                parts.append(option_string)
            parts[-1] += f" {args_string}"
        # Add space at start of format string if there is no short option
        if len(action.option_strings) > 0 and action.option_strings[0][1] == "-":
            parts[-1] = "    " + parts[-1]
        return ", ".join(parts)


def get_parser() -> argparse.ArgumentParser:
    # Create command line argument parser.
    parser = argparse.ArgumentParser(
        usage="%(prog)s [-m|-x|-r|-c|-o|-a|-l|-s] [-h] [-d|-p] [-g|-t] [-v|-n] FROM TO",
        description="""\
Move/copy/link multiple files by wildcard patterns.

The FROM pattern is a shell glob pattern, in which ‘*’ stands for any number
of characters and ‘?’ stands for a single character.

Use #[l|u]N in the TO pattern to get the string matched by the Nth
FROM pattern wildcard [lowercased|uppercased].

Patterns should be quoted on the command line.""",
        formatter_class=HelpFormatter,
        add_help=False,
    )
    global PROGRAM_NAME
    PROGRAM_NAME = parser.prog
    parser.add_argument(
        "--help",
        action="help",
        help="show this help message and exit",
    )
    parser.add_argument(
        "-V",
        "--version",
        action="version",
        version="%(prog)s "
        + VERSION
        + """
Copyright (C) 2023 Reuben Thomas <rrt@sc3d.org>
Copyright (c) 1990 Vladimir Lanin.
Licence GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.""",
    )

    parser.add_argument("from_pattern", metavar="FROM-PATTERN")
    parser.add_argument("to_pattern", metavar="TO-PATTERN")

    parser.add_argument(
        "-h",
        "--hidden",
        action="store_true",
        help="treat dot files normally",
    )
    parser.add_argument(
        "-D",
        "--makedirs",
        action="store_true",
        help="create non-existent directories",
    )

    mode_group = parser.add_argument_group("mode of operation")
    mode_group.add_argument(
        "-m",
        "--move",
        action="append_const",
        dest="mode",
        const=OpType.MOVE,
        help="move source file to target name",
    )
    mode_group.add_argument(
        "-x",
        "--copydel",
        action="append_const",
        dest="mode",
        const=OpType.COPYDEL,
        help="copy source to target, then delete source",
    )
    mode_group.add_argument(
        "-r",
        "--rename",
        action="append_const",
        dest="mode",
        const=OpType.RENAME,
        help="rename source to target in same directory",
    )
    mode_group.add_argument(
        "-c",
        "--copy",
        action="append_const",
        dest="mode",
        const=OpType.COPY,
        help="copy source to target, preserving source permissions",
    )
    mode_group.add_argument(
        "-o",
        "--overwrite",
        action="append_const",
        dest="mode",
        const=OpType.OVERWRITE,
        help="overwrite target with source, preserving target permissions",
    )
    mode_group.add_argument(
        "-l",
        "--hardlink",
        action="append_const",
        dest="mode",
        const=OpType.LINK,
        help="link target name to source file",
    )
    mode_group.add_argument(
        "-s",
        "--symlink",
        action="append_const",
        dest="mode",
        const=OpType.SYMLINK,
        help="symlink target name to source file",
    )

    delete_group = parser.add_argument_group(
        "how to handle file deletions and overwrites"
    )
    delete_group.add_argument(
        "-d",
        "--force",
        action="append_const",
        dest="deletion_mode",
        const=DeletionMode.FORCE,
        help="perform file deletes and overwrites without confirmation",
    )
    delete_group.add_argument(
        "-p",
        "--protect",
        action="append_const",
        dest="deletion_mode",
        const=DeletionMode.ABORT,
        help="treat file deletes and overwrites as errors",
    )

    error_group = parser.add_argument_group("how to handle erroneous actions")
    error_group.add_argument(
        "-g",
        "--go",
        action="append_const",
        dest="bad_op_mode",
        const=BadOpMode.SKIP,
        help="skip any erroneous actions",
    )
    error_group.add_argument(
        "-t",
        "--terminate",
        action="append_const",
        dest="bad_op_mode",
        const=BadOpMode.ABORT,
        help="erroneous actions are treated as errors",
    )

    report_group = parser.add_argument_group("reporting actions")
    report_group.add_argument(
        "-v", "--verbose", action="store_true", help="report all actions performed"
    )
    report_group.add_argument(
        "-n",
        "--dryrun",
        action="store_true",
        help="only report which actions would be performed",
    )

    return parser


class Glob:
    def __init__(self, glob_pat: str):
        parsed_glob = re.split(r"(\*\*|\*|\?|\[.[^]]+\])", glob_pat)
        self.num_wildcards = (len(parsed_glob) - 1) // 2
        re_pat = parsed_glob[0]
        for i in range(1, len(parsed_glob), 2):
            wildcard = parsed_glob[i]
            if wildcard == "**":
                wildcard_re = "(.*?)"
            elif wildcard == "*":
                wildcard_re = "([^/]*?)"
            elif wildcard == "?":
                wildcard_re = "(.)"
            else:  # wildcard is a character class
                # Globs use ! for negation, res use ^
                if wildcard_re[1] == "!":
                    wildcard_re = wildcard_re[0] + "^" + wildcard_re[2:]
                elif wildcard_re[1] == "^":
                    wildcard_re = wildcard_re[0] + r"\^" + wildcard_re[2:]
                wildcard_re = f"({wildcard_re})"
            re_pat += wildcard_re + parsed_glob[i + 1]
        re_pat += "$"
        self.re_text = re_pat  # for debugging
        self.re_pat = re.compile(re_pat)


class Replacement:
    def __init__(self, pat: str, num_globs: int) -> None:
        parsed_to = re.split(r"#([0-9]+)", pat)
        self.literal = [parsed_to[0]]
        self.backref = []
        for i in range(1, len(parsed_to), 2):
            globref = int(parsed_to[i])
            if globref > num_globs:
                die(1, f"to pattern contains invalid glob reference #{globref}")
            self.backref.append(globref)
            self.literal.append(parsed_to[i + 1])


class DeletionMode(Enum):
    ASK = auto()
    FORCE = auto()
    ABORT = auto()


class BadOpMode(Enum):
    ASK = auto()
    SKIP = auto()
    ABORT = auto()


class OpType(Enum):
    MOVE = auto()
    COPYDEL = auto()
    RENAME = auto()
    COPY = auto()
    OVERWRITE = auto()
    LINK = auto()
    SYMLINK = auto()


@dataclass
class Operation:
    from_path: Path
    to_path: Path
    operation: OpType


def from_to_to(filename: str, glob_pat: Glob, replacement: Replacement) -> Path:
    m = re.match(glob_pat.re_pat, filename)
    assert m
    repl_parts = [replacement.literal[0]]
    for i, backref in enumerate(replacement.backref):
        repl_parts.append(m[backref])
        repl_parts.append(replacement.literal[i + 1])
    return Path("".join(repl_parts))


class OperationError(Exception):
    def __init__(self, op: Operation) -> None:
        super().__init__()
        self.op = op


class TemporaryFiles:
    def __init__(self) -> None:
        self.uid = 0
        # pylint: disable=consider-using-with
        self.tmpdir = tempfile.TemporaryDirectory()

    def __enter__(self: Self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc: Optional[BaseException],
        traceback: Optional[TracebackType],
    ) -> None:
        self.tmpdir.cleanup()

    def temp_name(self, name: Path) -> Path:
        self.uid += 1
        return Path(self.tmpdir.name) / name.with_stem(f"{name.stem}_{self.uid}")


def make_temp_replacements(
    temp_files: TemporaryFiles,
    replacements: List[Operation],
) -> Tuple[List[Operation], List[Operation]]:
    initial_operations, final_operations = [], []
    for op in replacements:
        temp_to_name = temp_files.temp_name(op.to_path)
        initial_operations.append(Operation(op.from_path, temp_to_name, op.operation))
        final_operations.append(Operation(temp_to_name, op.to_path, op.operation))
    return initial_operations, final_operations


# pylint: disable=dangerous-default-value
def main(argv: List[str] = sys.argv[1:]) -> None:
    # Parse command line arguments
    args = get_parser().parse_args(argv)

    # Deal with mutually exclusive argument groups
    # (We do not use argparse.add_mutually_exclusive_group, since that lumps
    # all the --help output together)
    if args.mode is not None and len(args.mode) > 1:
        die(1, "at most one operation mode is allowed")
    args.mode = args.mode[0] if args.mode is not None else OpType.MOVE
    if args.deletion_mode is not None and len(args.deletion_mode) > 1:
        die(1, "at most one deletion mode is allowed")
    args.deletion_mode = (
        args.deletion_mode[0] if args.deletion_mode is not None else DeletionMode.ASK
    )
    if args.bad_op_mode is not None and len(args.bad_op_mode) > 1:
        die(1, "at most one deletion mode is allowed")
    args.bad_op_mode = (
        args.bad_op_mode[0] if args.bad_op_mode is not None else BadOpMode.ASK
    )

    # Parse patterns
    from_glob = Glob(args.from_pattern)
    replacement_pat = Replacement(args.to_pattern, from_glob.num_wildcards)

    # Find "from" files and generate "to" files
    replacements: List[Operation] = []
    for filename in glob.glob(args.from_pattern, recursive=True):
        replacements.append(
            Operation(
                filename, from_to_to(filename, from_glob, replacement_pat), OpType.MOVE
            )
        )
    num_collisions = 0
    for op in replacements:
        if any(op.to_path == repl.from_path for repl in replacements):
            num_collisions += 1
            warn(f"destination conflict: {op.from_path} -> {op.to_path}")
    if num_collisions > 0:
        die(1, 'stopping, as there are multiple "from" files with the same "to" name')
    if len(replacements) == 0:
        die(1, f"no matches for {args.from_pattern} -> {args.to_pattern}")

    # Check the replacements
    free_replacements = []
    clash_replacements = []
    with_temp_replacements = []
    for op in replacements:
        # A "from" that is also a "to" must be moved first.
        if any(op.from_path == repl.to_path for repl in replacements):
            if op.operation in (OpType.COPY, OpType.LINK):
                die(1, "stopping, as destination {to_path} already exists")
            clash_replacements.append(op)
        # If a replacement's "from" is in clash_replacements, we have a
        # cycle. Add the replacement to with_temp_replacements.
        elif any(op.from_path == repl.from_path for repl in clash_replacements):
            with_temp_replacements.append(op)
        # Otherwise, we can add to free_replacements.
        else:
            free_replacements.append(op)

    for op in replacements:
        # Check destinations do not exist already
        # FIXME: only if necessary, add prompt
        if op.to_path.exists():
            die(1, f"destination {op.to_path} already exists")
        # Check destination's parent directory exists
        if not args.makedirs and not op.to_path.parent.exists():
            die(1, f"parent directory of destination {op.to_path} does not exist")

    # FIXME: Prompt for deletions (if necessary)

    # Do the replacements
    # FIXME: support --dryrun and --verbose
    with TemporaryFiles() as tmp_files:
        initial_operations, final_operations = make_temp_replacements(
            tmp_files,
            with_temp_replacements,
        )
        operations: List[Operation] = []

        def do_operations(replacements: List[Operation]) -> None:
            for op in replacements:
                try:
                    if not args.dryrun:
                        # FIXME: Make parents directory if needed and
                        # args.makedirs is True
                        match op.operation:
                            case OpType.MOVE | OpType.COPYDEL | OpType.RENAME:
                                shutil.move(op.from_path, op.to_path)
                            case OpType.COPY:
                                shutil.copytree(op.from_path, op.to_path)
                            case OpType.OVERWRITE:
                                # FIXME: preserve target permissions
                                shutil.copytree(op.from_path, op.to_path)
                            case OpType.LINK:
                                os.link(op.from_path, op.to_path)
                            case OpType.SYMLINK:
                                os.symlink(op.from_path, op.to_path)
                        operations.append(op)
                    if args.dryrun or args.verbose:
                        print(
                            f"{op.from_path} -> {op.to_path}{' : done' if not args.dryrun else ''}"
                        )
                        # FIXME: Add " (*)" suffix if there's an overwrite
                        # FIXME: Add notation for when we're breaking a
                        # cycle (^ instead of >) or moving a link (= instead
                        # of -)
                except IOError as e:
                    raise OperationError(op) from e

        try:
            do_operations(initial_operations)
            do_operations(clash_replacements)
            do_operations(free_replacements)
            do_operations(final_operations)
        except OperationError as e:
            warn(f"could not {e.op.operation} {e.op.from_path} -> {e.op.to_path}: {e}")
            # FIXME: Offer to carry on, stop or undo.
