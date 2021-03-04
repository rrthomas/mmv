# Possible defines in CONF:
#	IS_MSDOS IS_SYSV IS_V7 IS_BSD HAS_DIRENT HAS_RENAME MV_DIR

CC		=gcc
LD		=$(CC)
CONF		=-DIS_SYSV -DHAS_DIRENT -DHAS_RENAME
CFLAGS		=-O2 $(CONF)
LDFLAGS		=-s -N

#IBIN		=$(LOCAL)$(ARCH)/bin
#IMAN		=$(LOCAL)$(ANY)/man
IBIN=$(DESTDIR)/usr/bin/
IMAN=$(DESTDIR)/usr/share/man/

mmv:		mmv.o

clean:
	rm -f mmv mmv.o

install:	$(DEST)$(IBIN)/mmv
install:	$(DEST)$(IMAN)/man1/mmv.1

$(DEST)$(IBIN)/mmv:		mmv;	cp $? $@
$(DEST)$(IMAN)/man1/mmv.1:	mmv.1;	cp $? $@
