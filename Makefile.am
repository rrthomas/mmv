# Top-level Makefile.am
#
# Copyright (c) 2021 Reuben Thomas <rrt@sc3d.org>
#
# This file is part of mmv.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.

ACLOCAL_AMFLAGS=-I m4

SUBDIRS = lib

dist_doc_DATA = README

LDADD = $(top_builddir)/lib/libgnu.a
AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_builddir)/lib
AM_CFLAGS = $(WARN_CFLAGS)

man_MANS = mmv.1

bin_PROGRAMS = mmv$(EXEEXT)
mmv_SOURCES = mmv.c
mmv_LDADD = $(LDADD)

SYMLINKS = mcp$(EXEEXT) mln$(EXEEXT) mad$(EXEEXT)
MAKELINKS = for n in $(SYMLINKS); do $(RM) $$n$(EXEEXT) && $(LN_S) mmv$(EXEEXT) $$n; done

EXTRA_DIST = $(man_MANS)

all-local: mmv$(EXEEXT)
	$(MAKELINKS)

install-exec-hook:
	cd $(DESTDIR)$(bindir) && $(MAKELINKS)

uninstall-hook:
	cd $(DESTDIR)$(bindir) && $(RM) $(SYMLINKS)

clean-local:
	$(RM) $(SYMLINKS)

release: distcheck
	git diff --exit-code && \
	git tag -a -m "Release tag" "v$(VERSION)" && \
	git push && git push --tags && \
	woger github package=$(PACKAGE) version=$(VERSION) dist_type=tar.gz
