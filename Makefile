CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Wshadow `pkg-config --cflags libgit2`

OBJS = socket.o

LDFLAGS = `pkg-config --libs libgit2`

.SUFFIXES :
.SUFFIXES : .o .c

gitftp : gitftp.c socket.o
	$(CC) $(CFLAGS) -o $@ gitftp.c $(LDFLAGS) $(OBJS)

socket.o : socket.c socket.h
