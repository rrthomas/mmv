# mmv

by Reuben Thomas <rrt@sc3d.org>  

mmv is a program to move/copy/link multiple files according to a set of
wildcard patterns. This multiple action is performed safely, i.e. without
any unexpected deletion of files due to collisions of target names with
existing filenames or with other target names. Furthermore, before doing
anything, mmv attempts to detect any errors that would result from the
entire set of actions specified and gives the user the choice of either
aborting before beginning, or proceeding by avoiding the offending parts.

mmv is distributed under the terms of the GNU General Public License; either
version 3 of the License, or (at your option), any later version. See the
file COPYING for more details.


## Installation

The easiest way to install PSUtils is from PyPI, the Python Package Index:

`pip install mmv`


### Building from Git

mmv requires Python 3.10 or later and some Python libraries (listed in
`pyproject.toml`, and automatically installed by the build procedure).

In the source directory: `python -m build` (requires the `build` package to
be installed).

You can then install it with `pip install .`.


## Use

See `mmv(1)` (run `man mmv`).
