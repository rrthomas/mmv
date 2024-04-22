# Makefile.am
#
# Copyright (c) 2021-2024 Reuben Thomas <rrt@sc3d.org>
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

AM_VALAFLAGS = --debug --vapidir=$(srcdir)

SUBDIRS = lib

dist_doc_DATA = README.md

LDADD = $(top_builddir)/lib/libgnu.a
AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_builddir)/lib
AM_CFLAGS = $(WARN_CFLAGS)

dist_man_MANS = mmv.1
MAN_SYMLINKS = mad.1 mcp.1 mln.1

bin_PROGRAMS = mmv$(EXEEXT)
mmv_SOURCES = mmv_c.c cmdline.c cmdline.h mmv.vala $(BUILT_SOURCES)
mmv_LDADD = $(LDADD) $(GLIB_LIBS)
mmv_CFLAGS = --include config.h $(GLIB_CFLAGS)
mmv_VALAFLAGS = $(AM_VALAFLAGS) --pkg config --pkg gee-0.8 --pkg posix --pkg gnu --pkg cmdline --pkg mmv
VAPIS =	config.vapi cmdline.vapi mmv.vapi

SYMLINKS = mcp$(EXEEXT) mln$(EXEEXT) mad$(EXEEXT)
MAKELINKS = for n in $(SYMLINKS); do $(RM) $$n$(EXEEXT) && $(LN_S) mmv$(EXEEXT) $$n; done

EXTRA_SRCS = opts.ggo $(VAPIS)
EXTRA_DIST = $(man_MANS) $(EXTRA_SRCS) m4/gnulib-cache.m4 mmv-include.man build-aux/mmv-help2man-wrapper

BUILT_SOURCES = dummy.vala

dummy.vala: $(VAPIS) $(BUILT_VAPIS)
	touch $@

DISTCLEANFILES = $(man_MANS)

mmv.1: mmv.c build-aux/mmv-help2man-wrapper mmv-include.man
## Exit gracefully if $@ is not writeable, such as during distcheck!
	$(AM_V_GEN)if ( touch $@.w && rm -f $@.w; ) >/dev/null 2>&1; then \
	  $(top_srcdir)/build-aux/missing --run $(HELP2MAN) --no-info \
		--no-discard-stderr \
		--include=$(srcdir)/mmv-include.man \
		--output=$@ $(srcdir)/build-aux/mmv-help2man-wrapper; \
	fi

mmv.c: cmdline.h

cmdline.h cmdline.c: $(top_srcdir)/opts.ggo
	gengetopt < $(top_srcdir)/opts.ggo --unamed-opts

all-local: mmv$(EXEEXT)
	$(MAKELINKS)

install-data-hook:
	for n in $(MAN_SYMLINKS); do \
		cd $(DESTDIR)$(mandir)/man1 && $(RM) $$n && $(LN_S) mmv.1 $$n; \
	done

install-exec-hook:
	cd $(DESTDIR)$(bindir) && $(MAKELINKS)

uninstall-hook:
	cd $(DESTDIR)$(bindir) && $(RM) $(SYMLINKS)
	for n in $(MAN_SYMLINKS); do \
		$(RM) $(DESTDIR)$(mandir)/man1/$$n; \
	done

clean-local:
	$(RM) $(SYMLINKS)

release: distcheck
	git diff --exit-code && \
	git tag -a -m "Release tag" "v$(VERSION)" && \
	git push && git push --tags && \
	woger github package=$(PACKAGE) version=$(VERSION) dist_type=tar.gz
