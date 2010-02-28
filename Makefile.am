lib_LTLIBRARIES = lib_mysqludf_dom.la
lib_mysqludf_dom_la_SOURCES = dom.c ns.c
lib_mysqludf_dom_la_CFLAGS = -DSTANDARD -DMYSQL_SERVER @MYSQL_CFLAGS@ @XML2_INCLUDEDIR@
lib_mysqludf_dom_la_LDFLAGS = -module -avoid-version -no-undefined -rpath $(pkglibdir)  @XML2_LIBDIR@ @XML2_LIBS@

EXTRA_DIST = test *.sql API *.h

.PHONY : test

mrproper:
	make clean
	make maintainer-clean
	rm -rf config.guess config.h.* config.status configure missing config.sub ltmain.sh depcomp aclocal.m4 install.sh config.log compile Makefile.in doc/html *.loT mkinstalldirs install-sh *~

installdb: 
	$(MYSQL) < ./installdb.sql

uninstalldb: 
	$(MYSQL) < ./uninstalldb.sql
	
dist-hook:
	find $(distdir) -name .svn -o -name .DS_Store -o -name .cdtproject -o -name .project -o -name .settings | xargs --no-run-if-empty rm -rf

test: 
	for i in `ls test/*.result | awk -F "." '{print $$1}'`; do echo "$$i"; $(MYSQLTEST) --result $$i.result < $$i.test; done

cleantest:
	rm test/*.log test/*.reject
