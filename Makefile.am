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

LDADD = $(top_builddir)/lib/libgnu.a $(LIBGC_LIBS)
AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_builddir)/lib
AM_CFLAGS = $(WARN_CFLAGS) $(LIBGC_FLAGS)

man_MANS = mmv.1

bin_PROGRAMS = mmv$(EXEEXT)
mmv_SOURCES = mmv.c cmdline.c cmdline.h
mmv_LDADD = $(LDADD)

SYMLINKS = mcp$(EXEEXT) mln$(EXEEXT)
MAKELINKS = for n in $(SYMLINKS); do $(RM) $$n$(EXEEXT) && $(LN_S) mmv$(EXEEXT) $$n; done

EXTRA_SRCS = opts.ggo
EXTRA_DIST = $(man_MANS) $(EXTRA_SRCS) m4/gnulib-cache.m4 mmv-include.man build-aux/mmv-help2man-wrapper

DISTCLEANFILES = $(man_MANS)

mmv.1: $(builddir)/mmv$(EXEEXT) build-aux/mmv-help2man-wrapper mmv-include.man
## Exit gracefully if $@ is not writeable, such as during distcheck!
	$(AM_V_GEN)if ( touch $@.w && rm -f $@.w; ) >/dev/null 2>&1; then \
	  $(top_srcdir)/build-aux/missing --run $(HELP2MAN) --no-info \
		--no-discard-stderr \
		--include=$(srcdir)/mmv-include.man \
		--output=$@ $(srcdir)/build-aux/mmv-help2man-wrapper; \
	fi

mmv.o: cmdline.h

cmdline.h cmdline.c: opts.ggo
	gengetopt < opts.ggo --unamed-opts

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
