# $NOWHERE: Makefile,v 1.3 2002/01/18 22:15:45 mickey Exp $

PROG=	nomadio
CFLAGS+=-Wall -Werror
PREFIX?=/usr/local
BINDIR= ${PREFIX}/bin
MANDIR= ${PREFIX}/man/cat

.include <bsd.prog.mk>
