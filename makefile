## makefile for ZipRec

INCDIR=./framepac

OBJS = build/bits.o \
	build/chartype.o \
	build/dbyte.o \
	build/dbuffer.o \
	build/huffman.o \
	build/index.o \
	build/inflate.o \
	build/lenmodel.o \
	build/loclist.o \
	build/models.o \
	build/packet.o \
	build/partial.o \
	build/pstrie.o \
	build/recover.o \
	build/reconstruct.o \
	build/symtab.o \
	build/ui_curses.o \
	build/words.o \
	build/global.o \
	build/scan_ziprec.o \
	build/wildcard.o

ALLOBJS = build/ziprec.o build/ziprecui.o $(OBJS)

EXES = bin/ziprec bin/mklang

LIBS = whatlang2/langident.a framepac/framepacng.a

DISTFILES =  COPYING CHANGELOG ziprec-doc.txt mklang-doc.txt makefile *.h *.C \
		framepac/COPYING framepac/LICENSE framepac/makefile \
		framepac/*.h framepac/framepac/*.h framepac/src/*.C \
		framepac/template/*.cc framepac/tests/*.C framepac/tests/*.sh \
		whatlang2/COPYING whatlang2/README \
		whatlang2/makefile whatlang2/*.h whatlang2/*.C whatlang2/*.txt \
		Europarl-eval/README Europarl-eval/*.sh

LIBRARY = build/ziprec$(LIBEXT)

#########################################################################
## define the compiler

CC=g++ --std=c++11
CCLINK=$(CC)

#########################################################################
## define compilation options

ifeq ($(NDEBUG),1)
NODEBUG=-DNDEBUG
else
NODEBUG=
endif

ifndef BITS
#BITS=32
BITS=64
endif

ifndef PROFILE
#PROFILE=-pg
#PROFILE=-DPURIFY
endif

ifeq ($(DEBUG),1)
CFLAGS = -Wall -Wextra -O0 -fno-inline -ggdb3 -m$(BITS) $(PROFILE)
else
CFLAGS = -Wall -Wextra -O3 -fexpensive-optimizations -ggdb3 -m$(BITS) $(NODEBUG) $(PROFILE)
endif

ifndef RELEASE
RELPATH=ZipRec
ZIPNAME=ziprec.zip
else
RELPATH=ZipRec-$(RELEASE)
ZIPNAME=ziprec-$(RELEASE).zip
endif

CLINK=
CPUTYPE = -D__386__ -D__586__ -D__886__

RM=rm -f

LIBEXT=.a

#########################################################################
## standard build targets

.PHONY: all
all: $(EXES) null.lang

.PHONY: clean
clean:
	-$(RM) $(ALLOBJS) $(EXES)
	-$(RM) build/mklang.o mklang

.PHONY: allclean
allclean: clean
	-$(RM) bin/* build/*
	-( cd framepac ; $(MAKE) clean )
	-( cd whatlang2 ; $(MAKE) clean )

.PHONY: tags
tags:
	etags --c++ *.h *.C

.PHONY: zip
zip:	$(EXES) whatlang2/bin/mklangid
	-$(RM) $(ZIPNAME)
	mkdir $(RELPATH)
	-cp -ip --parents $(DISTFILES) $(RELPATH)
	-( cd $(RELPATH) ; md5sum ${DISTFILES} >MD5SUM )
	-( cd $(RELPATH) ; sha1sum ${DISTFILES} >SHA1SUM )
	zip -mro9q $(ZIPNAME) $(RELPATH)/

#########################################################################
## executables

bin/ziprec: build/ziprec.o $(LIBRARY) $(LIBS)
	@mkdir -p bin
	$(CC) -o $@ $(CFLAGS) $(CLINK) $^ -lrt

bin/mklang: build/mklang.o $(LIBRARY) $(LIBS)
	@mkdir -p bin
	$(CC) -o $@ $(CFLAGS) $(CLINK) $^ -pthread -lrt

whatlang2/bin/mklangid:
	( cd whatlang2 ; $(MAKE) all )

#########################################################################
## libraries

framepac/framepacng.a: nonexistentfile
	( cd framepac ; $(MAKE) DEBUG=$(DEBUG) lib )

whatlang2/langident.a:  nonexistentfile
	( cd whatlang2 ; $(MAKE) lib )

.PHONY: nonexistentfile
nonexistentfile:

$(LIBRARY): $(OBJS)
	-$(RM) $@
	ar rucl $@ $^
	ranlib $@

#########################################################################
## data files

null.lang: bin/mklang
	bin/mklang null.lang /dev/null

#########################################################################
## object modules

build/bits.o: 		bits.C bits.h global.h

build/chartype.o: 	chartype.C chartype.h

build/dbyte.o: 		dbyte.C dbyte.h global.h

build/dbuffer.o: 	dbuffer.C dbuffer.h inflate.h global.h

build/global.o: 	global.C global.h

build/huffman.o: 	huffman.C huffman.h global.h

build/index.o: 		index.C index.h

build/inflate.o: 	inflate.C inflate.h dbuffer.h loclist.h models.h partial.h recover.h \
			reconstruct.h symtab.h words.h global.h whatlang2/langid.h

build/lenmodel.o: 	lenmodel.C lenmodel.h

build/loclist.o: 	loclist.C loclist.h

build/models.o: 	models.C models.h dbuffer.h global.h wildcard.h

build/packet.o: 	packet.C inflate.h

build/partial.o: 	partial.C partial.h bits.h inflate.h symtab.h global.h

build/pstrie.o:		pstrie.C pstrie.h wildcard.h

build/reconstruct.o: 	reconstruct.C reconstruct.h dbuffer.h index.h global.h \
			models.h wildcard.h

build/recover.o: 	recover.C recover.h inflate.h loclist.h reconstruct.h global.h

build/scan_ziprec.o: 	scan_ziprec.C

build/symtab.o:		symtab.C symtab.h inflate.h global.h

build/ui_curses.o:	ui_curses.C ui_curses.h

build/wildcard.o:	wildcard.C wildcard.h

build/words.o: 		words.C words.h chartype.h

build/ziprec.o: 	ziprec.C inflate.h models.h recover.h reconstruct.h global.h

build/mklang.o: 	mklang.C global.h pstrie.h wildcard.h words.h ziprec.h whatlang2/langid.h

dbuffer.h: 		dbyte.h
	touch $@

dbyte.h:		framepac/framepac/file.h
	touch $@

global.h: 		dbyte.h
	touch $@

huffman.h:		bits.h framepac/framepac/smartptr.h
	touch $@

index.h: 		dbyte.h
	touch $@

inflate.h: 		huffman.h framepac/framepac/file.h
	touch $@

models.h: 		dbyte.h dbuffer.h pstrie.h whatlang2/langid.h
	touch $@

pstrie.h:		whatlang2/ptrie.h whatlang2/trie.h framepac/framepac/byteorder.h \
			framepac/framepac/file.h
	touch $@

recover.h: 		lenmodel.h ziprec.h
	touch $@

symtab.h:		huffman.h framepac/framepac/memory.h framepac/framepac/smartptr.h
	touch $@

ui_common.h:		ui.h
	touch $@

ui_curses.h:		ui_common.h
	touch $@

ziprec.h: dbyte.h
	touch $@

#########################################################################
## default compilation rule

.C.o:
	$(CC) $(CFLAGS) $(CPUTYPE) -I$(INCDIR) -c $<

build/%.o : %.C
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPUTYPE) -I$(INCDIR) -c -o $@ $<
