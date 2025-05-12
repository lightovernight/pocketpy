#pragma once
#include "pocketpy/export.h"
#include <stdint.h>


#define INVALID_SOCKET_HANDLER (void*)(uintptr_t)(-1)

typedef void* socket_handler;
enum pk_address_family{
    PK_AF_INET = 2
};

enum pk_socket_kind {
    PK_SOCK_STREAM = 1
};

PK_API int socket_init();

PK_API int socket_clean();

PK_API socket_handler socket_create(int family, int type, int protocol);

PK_API int socket_bind(socket_handler socket, const char* hostname, unsigned short port);

PK_API int socket_listen(socket_handler socket, int backlog);

PK_API socket_handler socket_accept(socket_handler socket, char* client_ip, unsigned short* client_port);

PK_API int socket_connect(socket_handler socket, const char* server_ip, unsigned short server_port);

PK_API int socket_recv(socket_handler socket, char* buffer, int maxsize);

PK_API int socket_send(socket_handler socket, const char* senddata, int datalen);

PK_API int socket_close(socket_handler socket);

PK_API int socket_set_block(socket_handler socket,int flag);


PK_API int socket_get_last_error();



