#ifndef SOCKET_H
#define SOCKET_H

#include <stdio.h>

int negotiate_listen(char *svc);
int listen_or_die(char *svc);
int get_ip(int sock, int *ip);
int get_port(int sock);
FILE *accept_stream(int sock, char *mode);

#endif
