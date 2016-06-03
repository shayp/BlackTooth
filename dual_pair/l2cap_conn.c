#include "l2cap_conn.h"

#include <stdio.h>
#include <pthread.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <fcntl.h>
#include <errno.h>

#define BDADDR_STR_LEN 18

int connect_l2cap(char* dest_mac, short psm, int* p_omtu, int* p_imtu)
{
	struct sockaddr_l2 remote_addr = {0};
	int l2cap_sock = -1, err;

	l2cap_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

	if (l2cap_sock < 0)
	{
		printf("Failed to create L2CAP socket\n");
		return -1;
	}

	remote_addr.l2_family = AF_BLUETOOTH;
	remote_addr.l2_psm = htobs(psm);
	str2ba(dest_mac, &remote_addr.l2_bdaddr);

	printf("L2CAP::Connecting to target...");

	if ((err = connect(l2cap_sock, (struct sockaddr*)&remote_addr, sizeof(struct sockaddr_l2)) < 0))
	{
		printf("Failed to connect L2CAP socket (%d - %s)\n", errno, strerror(errno));
		close(l2cap_sock);
		return -1;
	}

	if (fcntl(l2cap_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make connected L2CAP socket nonblocking\n");
	}

	printf("success!\n");

	printf("L2CAP::Connected to %s @PSM %d", dest_mac, psm, l2cap_sock);

	if (NULL != p_omtu)
	{
		*p_omtu = get_l2cap_sock_mtu(l2cap_sock, 0);
		printf(" [OMTU: %d", *p_omtu);
	}

	if (NULL != p_imtu)
	{
		*p_imtu = get_l2cap_sock_mtu(l2cap_sock, 1);
		printf(" IMTU: %d", *p_imtu);
	}

	if (NULL != p_omtu || NULL != p_imtu)
	{
		printf("]");
	}

	printf("\n");

	return l2cap_sock;
} 

int listen_sock[30] = {0};

void* wait_for_l2cap_conn(void* sock_info)
{
	int received_client_sock = 0;
	uint32_t len;
	char remote_addr_str[BDADDR_STR_LEN];
	struct sockaddr_l2 local_addr;
	struct sockaddr_l2 remote_addr;
	int err;
	l2cap_sock_info* wanted_sock = (struct l2cap_sock_info*)sock_info;

	wanted_sock->res_sock = -1;

	if (0 == listen_sock[wanted_sock->psm])
	{
		printf("Creating l2 listen sock %d...\n", wanted_sock->psm);
		listen_sock[wanted_sock->psm] = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

		memset((uint8_t*)&local_addr, 0, sizeof(struct sockaddr_l2));

		local_addr.l2_family = AF_BLUETOOTH;
		local_addr.l2_bdaddr = *BDADDR_ANY;
		local_addr.l2_psm = htobs(wanted_sock->psm);	
	
		while (bind(listen_sock[wanted_sock->psm], (struct sockaddr*)&local_addr, sizeof(struct sockaddr_l2)) < 0)
		{
			printf("Failed to bind local L2CAP listening socket (%d - %s)\n", errno, strerror(errno));
			sleep(2);
		}
	
		printf("L2CAP::Sock bound\n");

		if ((err = listen(listen_sock[wanted_sock->psm], 1)) < 0)
		{
			printf("Failed to listen on L2CAP socket %d (%d - %s)\n", wanted_sock->psm, errno, strerror(errno));
			return NULL;
		}
	}

	printf("L2CAP::Listening...\n");

	len = sizeof(struct sockaddr_l2);
	received_client_sock = accept(listen_sock[wanted_sock->psm], (struct sockaddr*)&remote_addr, &len);

	printf("L2CAP::Accepted\n");

	if (received_client_sock < 0)
	{
		printf("Failed to accept L2CAP connection (%d - %s)\n", errno, strerror(errno));
		return NULL;
	}

	ba2str(&remote_addr.l2_bdaddr, remote_addr_str);

	wanted_sock->imtu = get_l2cap_sock_mtu(received_client_sock, 1);
	wanted_sock->omtu = get_l2cap_sock_mtu(received_client_sock, 0);

	printf("Opened L2CAP connection with %s [PSM %u IMTU: %d OMTU: %d]!\n", remote_addr_str, wanted_sock->psm, wanted_sock->imtu, wanted_sock->omtu);

	//close(listen_sock);

	if (fcntl(received_client_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make received L2CAP socket nonblocking\n");
	}

	wanted_sock->res_sock = received_client_sock;

	return NULL;
}

void start_l2cap_conn_wait(l2cap_sock_info* wanted_sock)
{
	pthread_t thread_id;

	pthread_create(&thread_id, NULL, wait_for_l2cap_conn, (void*)wanted_sock);
}

int get_l2cap_sock_mtu(int sock, int in_mtu)
{
	struct l2cap_options opts;
	int optlen = sizeof(opts);
	
	memset((uint8_t*)&opts, 0, sizeof(struct l2cap_options));

	if (getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) < 0)
		return DEFAULT_L2CAP_MTU;

	return (in_mtu) ? (opts.imtu) : (opts.omtu);
}

// in_mtu = 0 means setting the input mtu
void set_l2cap_sock_mtu(int sock, int wanted_mtu, int in_mtu)
{
	struct l2cap_options opts;
	int optlen = sizeof(opts);

	memset((uint8_t*)&opts, 0, sizeof(struct l2cap_options));

	if (getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) < 0)
		printf("Failed to get sock opt pre setting\n");

	if (in_mtu) 
		opts.imtu = wanted_mtu;
	else
		opts.omtu = wanted_mtu;

	if (setsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, optlen) < 0)
		printf("Failed to set sock opt\n");	
}

void send_l2cap_fragmented(int sock, uint8_t *buf, uint32_t len, uint32_t mtu)
{
	int offset, data_len;

	for (offset = 0; offset < len; offset += mtu)
	{
		if (offset + mtu > len)
			data_len = len - offset;
		else
			data_len = mtu;

		if (send(sock, buf + offset, data_len, 0) < 0) printf("send_l2cap_fragmented::send failed\n");
	}
}
