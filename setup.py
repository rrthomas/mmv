from build_manpages import build_manpages, get_install_cmd, get_build_py_cmd # type: ignore
from setuptools import setup

setup(
    cmdclass={
        'build_manpages': build_manpages,
        'build_py': get_build_py_cmd(),
        'install': get_install_cmd(),
    }
)
