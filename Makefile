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
