CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wshadow `pkg-config --cflags libgit2`

LDFLAGS = `pkg-config --libs libgit2`

.SUFFIXES :
.SUFFIXES : .o .c

gitftp : gitftp.c
