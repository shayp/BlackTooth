#ifndef SCO_CONN_H
#define SCO_CONN_H

#include <stdint.h>

#define DEFAULT_SCO_MTU 60

void start_sco_conn_wait(int* sock_addr);
int connect_sco(char* target_bdaddr_str, char* local_bdaddr);
void connect_sco_dual(int* wait_sock, char* local_bdaddr, char* target_bdaddr, int* on_connect_sock);
void send_sco_fragmented(int sock, uint8_t *buf, uint32_t len, uint32_t mtu);

#endif
