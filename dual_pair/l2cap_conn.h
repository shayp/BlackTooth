#ifndef L2CAP_CONN_H
#define L2CAP_CONN_H

#include <stdint.h>

#define DEFAULT_L2CAP_MTU 668 // 672, l2cap header is 4 B

typedef struct _l2cap_sock_info
{
	short psm;
	int res_sock;
	int omtu;
	int imtu;
} l2cap_sock_info;

int connect_l2cap(char* dest_mac, short psm, int* p_omtu, int* p_imtu);
void* wait_for_l2cap_conn(void* sock_info);
void start_l2cap_conn_wait(l2cap_sock_info* wanted_sock);
int get_l2cap_sock_mtu(int sock, int in_mtu);
void set_l2cap_sock_mtu(int sock, int wanted_mtu, int in_mtu);
void send_l2cap_fragmented(int sock, uint8_t *buf, uint32_t len, uint32_t mtu);

#endif
