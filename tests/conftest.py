from pytest import FixtureRequest, Parser, fixture


def pytest_addoption(parser: Parser) -> None:
    parser.addoption(
        "--regenerate-expected",
        action="store_true",
        help="regenerate the expected outputs",
    )

@fixture
def regenerate_expected(request: FixtureRequest) -> bool:
    opt = request.config.getoption("--regenerate-expected")
    assert isinstance(opt, bool)
    return opt
