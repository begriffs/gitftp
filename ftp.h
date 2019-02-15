#ifndef GITFTP_FTP_H
#define GITFTP_FTP_H

#include <stdio.h>

#include <git2/tree.h>

void git_or_die(FILE *conn, int code);
void ftp_ls(FILE *conn, git_tree *tr);
void pasv_format(const int *ip, int port, char *out);
void ftp_session(int sock, int server_ip[4], const char *gitpath);

#endif
