from pathlib import Path

from testutils import directory_test, make_tests, Case
from mmv import main as mmv

pytestmark = make_tests(
    mmv,
    Path(__file__).parent.resolve() / "test-files",
    Case(
        "simple-rename",
        ["a", "b"],
        Path(),
        Path(),
    ),
)
test_mmv = directory_test
