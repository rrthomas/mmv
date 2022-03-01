# mmv

by Vladimir Lanin  
maintained by Reuben Thomas <rrt@sc3d.org>  

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


## Installation and compatibility

mmv should work on any POSIX-1.2001-compatible system.

Reports on compatibility, whether positive or negative, are welcomed.


### Building from a release tarball

A C compiler and libgc are required to build from source. For building from
git, see below.

To build mmv from a release tarball, run

`./configure && make && make check`


### Building mmv from git

The GNU autotools are required: automake, autoconf and libtool.
[Gnulib](https://www.gnu.org/software/gnulib/) is also used, with a
third-party `bootstrap` module; these are installed automatically.
Finally, help2man is required to build the man page.

To build from a Git repository, first run

```
./bootstrap
```

Then see "Building from source" above.


## Use

See `mmv(1)` (run `man mmv`).
