#ifndef RFCOMM_CONN_H
#define RFCOMM_CONN_H

typedef struct _wanted_rfcomm_sock
{
	int channel;
	int res_sock;
} wanted_rfcomm_sock_res;

void start_rfcomm_conn_wait(wanted_rfcomm_sock_res* sock_addr);
void* wait_for_rfcomm_conn(void* sock_addr);
int connect_rfcomm(char* target_addr, char* local_addr, int channel);

#endif
