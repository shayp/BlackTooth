#include "rfcomm_conn.h"
#include <stdio.h>
#include <pthread.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <fcntl.h>
#include <errno.h>

#define BDADDR_STR_LEN 18 /* XX:XX:XX:XX:XX:XX */

void* wait_for_rfcomm_conn(void* sock_addr)
{
	int accepter, accepterlen, rfcomm_sock;
	wanted_rfcomm_sock_res* res = (wanted_rfcomm_sock_res*)sock_addr;
	struct sockaddr_rc remote2;
	char remote_addr_str[BDADDR_STR_LEN];

	res->res_sock = -1;

	accepter = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	remote2.rc_family = AF_BLUETOOTH;
	remote2.rc_channel = res->channel;
	remote2.rc_bdaddr = *BDADDR_ANY;

	printf("Waiting on RFCOMM2\n");

	if (bind(accepter, (struct sockaddr*)&remote2, sizeof(struct sockaddr_rc)) < 0) 
	{
		printf("Failed to bind rfcomm\n");
		return NULL;
	}

	if (listen(accepter, 1) < 0) 
	{
		printf("Failed to listen on rfcomm\n");
		return NULL;
	}

	accepterlen = sizeof(struct sockaddr_rc);
	rfcomm_sock = accept(accepter, (struct sockaddr*)&remote2, &accepterlen);

	ba2str(&remote2.rc_bdaddr, remote_addr_str);

	printf("Accepted RFCOMM connection from %s!\n", remote_addr_str);
	close(accepter);

	if (fcntl(rfcomm_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make RFCOMM socket nonblocking\n");
		close(rfcomm_sock);
		return -1;
	}
	
	res->res_sock = rfcomm_sock;

	return NULL;
}

void start_rfcomm_conn_wait(wanted_rfcomm_sock_res* sock_addr)
{
	pthread_t thread_id;

	pthread_create(&thread_id, NULL, wait_for_rfcomm_conn, (void*)sock_addr);
}

int connect_rfcomm(char* target_addr, char* local_addr, int channel)
{
	int err, rfcomm_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);	
	struct sockaddr_rc remote;		

	remote.rc_family = AF_BLUETOOTH;
	remote.rc_channel = channel;
	str2ba(target_addr, &remote.rc_bdaddr);

	printf("RFCOMM::Connecting...\n");

	if ((err = connect(rfcomm_sock, (struct sockaddr *)&remote, sizeof(struct sockaddr_rc))) < 0)
	{
		printf("Failed to connect RFCOMM (%d)\n", err);
		close(rfcomm_sock);
		return -1;
	}

	if (fcntl(rfcomm_sock, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make RFCOMM socket nonblocking\n");
		close(rfcomm_sock);
		return -1;
	}

	return rfcomm_sock;
}

