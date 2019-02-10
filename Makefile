CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Wshadow `pkg-config --cflags libgit2`

LDFLAGS = `pkg-config --libs libgit2`

.SUFFIXES :
.SUFFIXES : .o .c

gitftp : gitftp.c
