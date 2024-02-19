.POSIX:

.SUFFIXES :
.SUFFIXES : .o .c

CFLAGS = -std=c99 -pedantic -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-missing-field-initializers

include config.mk

OBJS = socket.o ftp.o path.o

gitftp : gitftp.c $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ gitftp.c $(LDLIBS) $(OBJS)

socket.o : socket.c socket.h
ftp.o : ftp.c ftp.h
path.o : path.c path.h
