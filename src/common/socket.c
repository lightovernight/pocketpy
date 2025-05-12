#include "pocketpy/common/socket.h"

#if defined(PY_SYS_PLATFORM) && PY_SYS_PLATFORM == 0
#include <WinSock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_fd;
#elif PY_SYS_PLATFORM == 3 || PY_SYS_PLATFORM == 5
#include <sys/socket.h>
typedef int socket_fd;
#else
#error "Unsupported Platform"
#endif

#define SOCKET_HANDLERTOFD(handler) (socket_fd)(uintptr_t)(handler)
#define SOCKET_FDTOHANDLER(fd) (socket_handler)(uintptr_t)(fd)

int socket_init(){
    #if PY_SYS_PLATFORM == 0
        WORD sockVersion = MAKEWORD(2,2);
	    WSADATA wsaData;
        return WSAStartup(sockVersion, &wsaData); 
    #endif 
    return 0;
}

int socket_clean(){
    #if PY_SYS_PLATFORM == 0
        return WSACleanup();
    #endif
    return 0;
}

socket_handler socket_create(int family, int type, int protocol){
    return SOCKET_FDTOHANDLER(socket(family, type, protocol));
}

int socket_bind(socket_handler socket, const char* hostname, unsigned short port){
    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    inet_pton(AF_INET,hostname,&bind_addr.sin_addr);
    if(bind(SOCKET_HANDLERTOFD(socket), (const struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1){
        return -1;
    }
    return 0;
}

int socket_listen(socket_handler socket, int backlog){
    listen(SOCKET_HANDLERTOFD(socket), backlog);
    return 0;
}

socket_handler socket_accept(socket_handler socket, char* client_ip, unsigned short* client_port){
    struct sockaddr_in client_addr;
    socklen_t sockaddr_len = sizeof(client_addr);
    socket_fd client_socket = accept(SOCKET_HANDLERTOFD(socket), (struct sockaddr*)&client_addr, &sockaddr_len);
    if(client_ip != NULL){
        inet_ntop(AF_INET, &client_addr.sin_addr,client_ip,sizeof("255.255.255.255"));
    }
    if(client_port != NULL){
        *client_port = ntohs(client_addr.sin_port);
    }
    return SOCKET_FDTOHANDLER(client_socket);
}
int socket_connect(socket_handler socket, const char* server_ip, unsigned short server_port){
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    if(connect(SOCKET_HANDLERTOFD(socket), (const struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        return -1;
    }
    return 0;
}

int socket_recv(socket_handler socket, char* buffer, int maxsize){
    return recv(SOCKET_HANDLERTOFD(socket), buffer, maxsize,0);
}

int socket_send(socket_handler socket, const char* senddata, int datalen){
    return send(SOCKET_HANDLERTOFD(socket), senddata, datalen, 0);
}

int socket_close(socket_handler socket){
    #if PY_SYS_PLATFORM == 0
        return closesocket(SOCKET_HANDLERTOFD(socket));
    #elif PY_SYS_PLATFORM == 3 || PY_SYS_PLATFORM == 5
        return close(socket);
    #endif
}

int socket_set_block(socket_handler socket,int flag){
    #if PY_SYS_PLATFORM == 0
        u_long mode = flag == 1 ? 0 : 1;
        return ioctlsocket(SOCKET_HANDLERTOFD(socket), FIONBIO, &mode);
    #elif PY_SYS_PALTFORM == 3 || PY_SYS_PALTFORM == 5
        int flags = fcntl(sock, F_GETFL, 0);
        return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
}


int socket_get_last_error(){
    #if PY_SYS_PLATFORM == 0
        return WSAGetLastError();
    #elif PY_SYS_PLATFORM == 3 || PY_SYS_PLATFORM == 5
        return error
    #else
        #error should not reach here
    #endif
}