#ifndef WSWAN_NET_LINK_H
#define WSWAN_NET_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicia el servidor (Host) escuchando en el puerto indicado (ej. 5555)
bool NetLink_StartHost(int port);

// Acepta de forma no bloqueante a un cliente si está esperando conectarse
bool NetLink_PollAccept(void);

// Se conecta como Cliente a la IP y puerto del Host
bool NetLink_ConnectToHost(const char* ip, int port);

// Cierra cualquier socket abierto y limpia el estado
void NetLink_Disconnect(void);

// Devuelve true si hay dos consolas enlazadas activamente
bool NetLink_IsConnected(void);

// Envía 1 byte al otro dispositivo (true si se envió, false si el buffer está lleno o desconectado)
bool NetLink_SendByte(uint8_t data);

// Lee 1 byte si hay datos disponibles (true si leyó 1 byte en *out_data, false si no hay nada aún)
bool NetLink_RecvByte(uint8_t* out_data);

#ifdef __cplusplus
}
#endif

#endif // WSWAN_NET_LINK_H
