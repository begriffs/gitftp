#ifndef GITFTP_SOCKET_H
#define GITFTP_SOCKET_H

#include <stdio.h>

int negotiate_listen(const char *svc);
int get_ip_port(int sock, int *ip, int *port);
FILE *sock_stream(int sock, const char *mode);

#endif
