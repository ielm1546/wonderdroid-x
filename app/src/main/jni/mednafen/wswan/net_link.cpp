#include "net_link.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

static int s_listen_fd = -1;
static int s_peer_fd = -1;
static bool s_is_host = false;

static void set_nonblocking_and_nodelay(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // Desactiva el algoritmo de Nagle para enviar cada byte al instante sin esperar paquetes grandes
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
}

bool NetLink_StartHost(int port)
{
    NetLink_Disconnect();

    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0)
        return false;

    int opt = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(s_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        NetLink_Disconnect();
        return false;
    }

    if (listen(s_listen_fd, 1) < 0)
    {
        NetLink_Disconnect();
        return false;
    }

    int flags = fcntl(s_listen_fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(s_listen_fd, F_SETFL, flags | O_NONBLOCK);

    s_is_host = true;
    return true;
}

bool NetLink_PollAccept(void)
{
    if (!s_is_host || s_listen_fd < 0 || s_peer_fd >= 0)
        return (s_peer_fd >= 0);

    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(s_listen_fd, (struct sockaddr*)&client_addr, &len);

    if (client_fd >= 0)
    {
        s_peer_fd = client_fd;
        set_nonblocking_and_nodelay(s_peer_fd);
        // Ya tenemos pareja; cerramos el socket de escucha para no aceptar terceros
        close(s_listen_fd);
        s_listen_fd = -1;
        return true;
    }

    return false;
}

bool NetLink_ConnectToHost(const char* ip, int port)
{
    NetLink_Disconnect();

    if (!ip)
        return false;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0)
    {
        close(fd);
        return false;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return false;
    }

    s_peer_fd = fd;
    s_is_host = false;
    set_nonblocking_and_nodelay(s_peer_fd);
    return true;
}

void NetLink_Disconnect(void)
{
    if (s_peer_fd >= 0)
    {
        close(s_peer_fd);
        s_peer_fd = -1;
    }
    if (s_listen_fd >= 0)
    {
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    s_is_host = false;
}

bool NetLink_IsConnected(void)
{
    if (s_is_host && s_peer_fd < 0 && s_listen_fd >= 0)
    {
        NetLink_PollAccept();
    }
    return (s_peer_fd >= 0);
}

bool NetLink_SendByte(uint8_t data)
{
    if (!NetLink_IsConnected())
        return false;

    ssize_t sent = send(s_peer_fd, &data, 1, MSG_NOSIGNAL);
    if (sent == 1)
        return true;

    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false;

    // Error real o desconexión
    NetLink_Disconnect();
    return false;
}

bool NetLink_RecvByte(uint8_t* out_data)
{
    if (!out_data || !NetLink_IsConnected())
        return false;

    uint8_t buf = 0;
    ssize_t recvd = recv(s_peer_fd, &buf, 1, 0);
    if (recvd == 1)
    {
        *out_data = buf;
        return true;
    }

    if (recvd == 0)
    {
        // El otro jugador cerró la conexión
        NetLink_Disconnect();
        return false;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return false;

    NetLink_Disconnect();
    return false;
}
