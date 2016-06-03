#include "sco_conn.h"
#include <stdio.h>
#include <pthread.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sco.h>
#include <fcntl.h>
#include <errno.h>

#define BDADDR_STR_LEN 18 /* XX:XX:XX:XX:XX:XX */

void* wait_for_sco_conn(void* sock_addr)
{
	int listen_sock = 0;
	int received_client_sock = 0;
	uint32_t len, optlen;
	char remote_addr_str[BDADDR_STR_LEN];
	struct sockaddr_sco local_addr;
	struct sockaddr_sco remote_addr;
	struct sco_options conn_opts;
	int err;

	listen_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);

	memset((uint8_t*)&local_addr, 0, sizeof(struct sockaddr_sco));

	local_addr.sco_family = AF_BLUETOOTH;
	local_addr.sco_bdaddr = *BDADDR_ANY;	

	if (bind(listen_sock, (struct sockaddr*)&local_addr, sizeof(struct sockaddr_sco)) < 0)
	{
		printf("Failed to bind local SCO listening socket\n");
		*((int*)sock_addr) = -1;
		return NULL;
	}

	if ((err = listen(listen_sock, 1)) < 0)
	{
		printf("Failed to listen on SCO socket (%d - %s)\n", errno, strerror(errno));
		*((int*)sock_addr) = -1;
		return NULL;
	}

	printf("Waiting on SCO connection...\n");

	len = sizeof(struct sockaddr_sco);
	received_client_sock = accept(listen_sock, (struct sockaddr*)&remote_addr, &len);

	if (received_client_sock < 0)
	{
		printf("Failed to accept SCO connection (%d - %s)\n", errno, strerror(errno));
		*((int*)sock_addr) = -1;
		return NULL;
	}

	ba2str(&remote_addr.sco_bdaddr, remote_addr_str);

	printf("Opened SCO connection with %s!\n", remote_addr_str);

	close(listen_sock);

	if (fcntl(received_client_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make received SCO socket nonblocking\n");
	}

	memset(&conn_opts, 0, sizeof(struct sco_options));
	optlen = sizeof(struct sco_options);

	if (getsockopt(received_client_sock, SOL_SCO, SCO_OPTIONS, &conn_opts, &optlen) < 0)
	{
		printf("Failed to get sock options...\n");
	}

	printf("Received SCO connection mtu: %u\n", conn_opts.mtu);

	*((int*)sock_addr) = received_client_sock;

	return NULL;
}

void start_sco_conn_wait(int* sock_addr)
{
	pthread_t thread_id;

	pthread_create(&thread_id, NULL, wait_for_sco_conn, (void*)sock_addr);
}

int connect_sco(char* target_bdaddr_str, char* local_bdaddr)
{
	int sco_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);
	struct sockaddr_sco local_addr;	
	struct sockaddr_sco remote_addr;

	memset(&local_addr, 0, sizeof(struct sockaddr_sco));
	local_addr.sco_family = AF_BLUETOOTH;
	str2ba(local_bdaddr, &local_addr.sco_bdaddr);

	if (bind(sco_sock, (struct sockaddr *) &local_addr, sizeof(struct sockaddr_sco)) < 0) 
	{
		printf("Failed to bind SCO socket\n");
		close(sco_sock);		
		return -1;
	}

	memset(&remote_addr, 0, sizeof(struct sockaddr_sco));
	remote_addr.sco_family = AF_BLUETOOTH;
	str2ba(target_bdaddr_str, &remote_addr.sco_bdaddr);
	printf("SCO::Connecting...");

	if (connect(sco_sock, (struct sockaddr *) &remote_addr, sizeof(struct sockaddr_sco)) < 0)
	{
		printf("Failed to establish SCO link (%d - %s)\n", errno, strerror(errno));
		close(sco_sock);		
		return -1;
	}

	if (fcntl(sco_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make connected SCO socket nonblocking\n");
		close(sco_sock);
	}

	printf("Connected SCO connection to %s!\n", target_bdaddr_str);

	return sco_sock;
}

void connect_sco_dual(int* wait_sock, char* local_bdaddr, char* target_bdaddr, int* on_connect_sock)
{
	*wait_sock = -2;

	if (-1 == *on_connect_sock)
		*on_connect_sock = connect_sco(target_bdaddr, local_bdaddr);

	start_sco_conn_wait(wait_sock);

	printf("Waiting for wait_sock...\n");

	while (*wait_sock == -2);

	if (-1 == *wait_sock)
	{
		return;
	}
/*
	start_sco_conn_wait(on_connect_sock);

	printf("Connected. Waiting for on_conn_sock...\n");

	while (*on_connect_sock == -2);
*/
}

void send_sco_fragmented(int sock, uint8_t *buf, uint32_t len, uint32_t mtu)
{
	int offset, data_len;

	for (offset = 0; offset < len; offset += mtu)
	{
		if (offset + mtu > len)
			data_len = len - offset;
		else
			data_len = mtu;

		if (send(sock, buf + offset, data_len, 0) < 0) printf("send_sco_fragmented::send failed (%d bytes)\n", data_len);
	}
}
