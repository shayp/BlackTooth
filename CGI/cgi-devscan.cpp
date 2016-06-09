#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <iostream>
#include "cgicc/Cgicc.h"
#include "cgicc/HTTPHTMLHeader.h"
#include "cgicc/HTMLClasses.h"

using namespace cgicc;
using namespace std;

#define IS_BIT_SET(byte, bit)	(byte & (1 << bit))
#define IS_BIT_IN_ARR_SET(arr, bit) (IS_BIT_SET(arr[bit/8], (bit%8)))

#define COD_BIT_LIMITED_DISCOVERABLE_MODE 13
#define COD_BIT_RESERVED0 14
#define COD_BIT_RESERVED1 15
#define COD_BIT_POSITIONING 16
#define COD_BIT_NETWORKING 17
#define COD_BIT_RENDERING 18
#define COD_BIT_CAPTURING 19
#define COD_BIT_OBJECT_TRANSFER 20
#define COD_BIT_AUDIO 21
#define COD_BIT_TELEPHONY 22
#define COD_BIT_INFORMATION 23

#define GET_MAJOR_DEV_CLASS(cod) (cod[1] & 0x1F)

#define MAJOR_DEV_MISC 0
#define MAJOR_DEV_COMPUTER 1
#define MAJOR_DEV_PHONE 2
#define MAJOR_DEV_LAN 3
#define MAJOR_DEV_AUDIO_VIDEO 4
#define MAJOR_DEV_PERIPHERAL 5
#define MAJOR_DEV_IMAGING 6
#define MAJOR_DEV_WEARABLE 7
#define MAJOR_DEV_TOY 8
#define MAJOR_DEV_HEALTH 9
#define MAJOR_DEV_UNCATEGORIZED 0x1F

#define GET_MINOR_DEV_CLASS(cod) ((cod[0] & 0xFC) >> 2)

#define COMPUTER_MINOR_DEV_UNCATEGORIZED 0
#define COMPUTER_MINOR_DEV_DESKTOP 1
#define COMPUTER_MINOR_DEV_SERVER 2
#define COMPUTER_MINOR_DEV_LAPTOP 3
#define COMPUTER_MINOR_DEV_HANDHELD_PC 4
#define COMPUTER_MINOR_DEV_PALM_PC 5
#define COMPUTER_MINOR_DEV_WEARABLE 6
#define COMPUTER_MINOR_DEV_TABLET 7

#define PHONE_MINOR_DEV_UNCATEGORIZED 0
#define PHONE_MINOR_DEV_CELLULAR 1
#define PHONE_MINOR_DEV_CORDLESS 2
#define PHONE_MINOR_DEV_SMARTPHONE 3
#define PHONE_MINOR_DEV_MODEM 4
#define PHONE_MINOR_DEV_ISDN_ACCESS 5

#define AUDIO_VIDEO_MINOR_DEV_UNCATEGORIZED 0
#define AUDIO_VIDEO_MINOR_DEV_HEADSET 1
#define AUDIO_VIDEO_MINOR_DEV_HANDS_FREE 2
#define AUDIO_VIDEO_MINOR_DEV_RESERVED0 3
#define AUDIO_VIDEO_MINOR_DEV_MICROPHONE 4
#define AUDIO_VIDEO_MINOR_DEV_LOUDSPEAKER 5
#define AUDIO_VIDEO_MINOR_DEV_HEADPHONES 6
#define AUDIO_VIDEO_MINOR_DEV_PORTABLE_AUDIO 7
#define AUDIO_VIDEO_MINOR_DEV_CAR_AUDIO 8
#define AUDIO_VIDEO_MINOR_DEV_SET_TOP_BOX 9
#define AUDIO_VIDEO_MINOR_DEV_HIFI_AUDIO 10
#define AUDIO_VIDEO_MINOR_DEV_VCR 11
#define AUDIO_VIDEO_MINOR_DEV_VIDEO_CAMERA 12
#define AUDIO_VIDEO_MINOR_DEV_CAMCORDER 13
#define AUDIO_VIDEO_MINOR_DEV_VIDEO_MONITOR 14
#define AUDIO_VIDEO_MINOR_DEV_VIDEO_AND_LOUDSPEAKER 15
#define AUDIO_VIDEO_MINOR_DEV_VIDEO_CONFERENCING 16
#define AUDIO_VIDEO_MINOR_DEV_RESERVED1 17
#define AUDIO_VIDEO_MINOR_DEV_TOY 18

#define GET_PERIPHERAL_MINOR_DEV_TYPE(cod) (cod[0] & 0xC0)
#define GET_PERIPHERAL_MINOR_DEV(cod) (cod[0] & 0x3C)

#define PERIPHERAL_MINOR_DEV_TYPE_UNCATEGORIZED 0
#define PERIPHERAL_MINOR_DEV_TYPE_KEYBOARD 1
#define PERIPHERAL_MINOR_DEV_TYPE_POINTER 2
#define PERIPHERAL_MINOR_DEV_TYPE_COMBO_KEYBOARD_POINTER 3

#define PERIPHERAL_MINOR_DEV_UNCATEGORIZED 0
#define PERIPHERAL_MINOR_DEV_JOYSTICK 1
#define PERIPHERAL_MINOR_DEV_GAMEPAD 2
#define PERIPHERAL_MINOR_DEV_REMOTE_CTL 3
#define PERIPHERAL_MINOR_DEV_SENSOR 4
#define PERIPHERAL_MINOR_DEV_DIGITIZER 5
#define PERIPHERAL_MINOR_DEV_CARD_READER 6
#define PERIPHERAL_MINOR_DEV_DIGITAL_PEN 7
#define PERIPHERAL_MINOR_DEV_BARCODE_SCANNER 8
#define PERIPHERAL_MINOR_DEV_GESTURE_INPUT 9

void add_service_string(uint8_t* cod, char* str, uint32_t buflen)
{
	uint8_t i, flag = 0;
	char* service_class_strs[] = 
	{
		"Limited Discoverable Mode",
		"Reserved0",
		"Reserved1",
		"Positioning",
		"Networking",
		"Rendering",
		"Capturing",
		"Object Transfer",
		"Audio",
		"Telephony",
		"Information"
	};

	//strcat(str, "Service Classes: ");


	// Go through bits 13-23 to determine set service classes
	for (i = COD_BIT_LIMITED_DISCOVERABLE_MODE; i <= COD_BIT_INFORMATION; i++)
	{
		if (IS_BIT_IN_ARR_SET(cod, i))
		{
			if (flag) strcat(str, ":");
			flag = 1;
			strcat(str, service_class_strs[i-COD_BIT_LIMITED_DISCOVERABLE_MODE]);
		}
	}	
}

void add_major_dev_string(uint8_t* cod, char* str, uint32_t buflen)
{
	uint8_t major = GET_MAJOR_DEV_CLASS(cod);
	char* major_classes_strs[] = 
	{
		"Miscellaneous",
		"Computer",
		"Phone",
		"LAN/Network Access Point",
		"Audio/Video",
		"Peripheral",
		"Imaging",
		"Wearable",
		"Toy",
		"Health",
		"Uncategorized"
	};
	
	//strcat(str, "Device Type: ");
	strcat(str, ",");
	strcat(str, major_classes_strs[major]);
}

void add_minor_dev_string(uint8_t* cod, char* str, uint32_t str_buf_len)
{
	uint8_t major = GET_MAJOR_DEV_CLASS(cod);
	uint8_t minor = GET_MINOR_DEV_CLASS(cod);
	char* computer_minor_class_strings[] =
	{
		"Uncategorized",
		"Desktop",
		"Server",
		"Laptop",
		"Handheld PC/PDA",
		"Palm-size PC/PDA",
		"Wearable Computer (watch size)",
		"Tablet"
	};
	char* phone_minor_class_strings[] =
	{
		"Uncategorized",
		"Cellular",
		"Cordless",
		"Smartphone",
		"Wired Modem",
		"Common ISDN Access"	
	};
	char* audio_minor_class_strings[] =
	{
		"Uncategorized",
		"Wearable Headset",
		"Hands-free Device",
		"Reserved0",
		"Microphone",
		"Loudspeaker",
		"Headphones",
		"Portable Audio",
		"Car Audio",
		"Set-top box",
		"HiFi Audio",
		"VCR",
		"Video Camera",
		"Camcorder",
		"Video Monitor",
		"Video Display and Loudspeaker",
		"Video Conferencing",
		"Reserved1",
		"Toy"
	};
	char* peripheral_primary_minor_class_strings[] = 
	{
		"Uncategorized",
		"Keyboard",
		"Pointing Device",
		"Combo Keyboard/Pointing Device"
	};
	char* peripheral_secondary_minor_class_strings[] = 
	{
		"Uncategorized",
		"Joystick",
		"Gamepad",
		"Remote Control",
		"Sensor",
		"Digitizer Tablet",
		"Card Reader",
		"Digital Pen",
		"Bar-code scanner",
		"Gestural Input Device"
	};

	//strcat(str, ", Secondary type: ");
	strcat(str, ",");	

	switch(major)
	{
		case MAJOR_DEV_COMPUTER:
		{
			strcat(str, computer_minor_class_strings[minor]);
			break;
		}
		case MAJOR_DEV_PHONE:
		{
			strcat(str, phone_minor_class_strings[minor]);
			break;
		}
		case MAJOR_DEV_AUDIO_VIDEO:
		{
			strcat(str, audio_minor_class_strings[minor]);
			break;
		}
		case MAJOR_DEV_PERIPHERAL:
		{
			uint8_t peripheral_type = GET_PERIPHERAL_MINOR_DEV_TYPE(cod);
			uint8_t peripheral = GET_PERIPHERAL_MINOR_DEV(cod);

			strcat(str, peripheral_primary_minor_class_strings[peripheral_type]);
			strcat(str, "/");
			strcat(str, peripheral_secondary_minor_class_strings[peripheral]);
			break;
		}
		default:
		{
			strcat(str, "Unknown");
			break;
		}
	};
}

void generate_class_string(uint8_t* cod, char* class_str, uint32_t str_buf_len)
{
	memset(class_str, 0, str_buf_len);

	// 1) Get service family string
	add_service_string(cod, class_str, str_buf_len);

	// 2) Get major device class
	add_major_dev_string(cod, class_str, str_buf_len);

	// 3) Get minor device class
	add_minor_dev_string(cod, class_str, str_buf_len);
}

int main(int argc, char **argv)
{
	// Stage 1: Device Inquiry
	inquiry_info *ii = NULL;
	int max_rsp, num_rsp;
	int dev_id, sock, len, flags;
	int i;
	char addr[19] = { 0 };
	char name[248] = { 0 };
	char class_string[512];
	struct hci_version ver;
	struct hci_conn_info_req* conn_info_req;
	uint8_t features[8];
	struct hci_dev_info dev_info;
	uint16_t packet_type;
	uint16_t connection_handle;
	uint8_t is_connected = 0;
	int8_t rssi;

	Cgicc cgi;
	
	cout << HTTPHTMLHeader() << endl;

	// 1.1: Get HCI device socket
	dev_id = hci_get_route(NULL);
	sock = hci_open_dev( dev_id );
	if (dev_id < 0 || sock < 0) 
	{
		perror("opening socket");
		exit(1);
	}

	len  = 3;
	max_rsp = 255;
	flags = IREQ_CACHE_FLUSH; // Find all devs
	ii = (inquiry_info*)malloc(max_rsp * sizeof(inquiry_info));

	// 1.2: Inquire HCI
	num_rsp = hci_inquiry(dev_id, len, max_rsp, NULL, &ii, flags);
	if( num_rsp < 0 ) perror("hci_inquiry");

	for (i = 0; i < num_rsp; i++) 
	{
		ba2str(&(ii+i)->bdaddr, addr);
		memset(name, 0, sizeof(name));

		if (0 != i) printf("\n");

		if (is_connected) 
		{
			hci_disconnect(sock, connection_handle, HCI_OE_USER_ENDED_CONNECTION, 25000);
			is_connected = 0;
		}

		if (hci_read_remote_name(sock, &(ii+i)->bdaddr, sizeof(name), name, 0) < 0)
		{
			strcpy(name, "Unknown");
		}

		generate_class_string(ii[i].dev_class, class_string, sizeof(class_string));
		printf("%s,%s,%02x%02x%02x,%s", name, addr, ii[i].dev_class[0], ii[i].dev_class[1], ii[i].dev_class[2], class_string);

		if (-1 == ioctl(sock, HCIGETCONNINFO, (unsigned long)conn_info_req))
		{
			if (-1 == hci_devinfo(dev_id, &dev_info))
			{
				printf(",[NO-CONN-INFO]");
				continue;
			}	

			packet_type = htobs(dev_info.pkt_type & ACL_PTYPE_MASK);

			if (-1 == hci_create_connection(sock, &ii[i].bdaddr, packet_type, 0, 1, &connection_handle, 25000)) 
			{
				printf(",[NO-CONN]\n");
				continue;
			}

			is_connected = 1;
		}
		else
		{
			connection_handle = htobs(conn_info_req->conn_info->handle);

			is_connected = 1;
		}

		if (-1 == hci_read_remote_version(sock, connection_handle, &ver, 5000))
			printf(",[NO-VER]");
		else
		{
			printf(",%s,0x%x,%s", 
			(0 == strlen(lmp_vertostr(ver.lmp_ver))) ? 
			("[NO-VERNAME]") : (lmp_vertostr(ver.lmp_ver)), 
			ver.lmp_subver, bt_compidtostr(ver.manufacturer));
		}


		if (-1 == hci_read_rssi(sock, connection_handle, &rssi, 5000))
			printf(",[NO-RSSI]");
		else
		{
			printf(",%i\n", rssi);
		}
		
	}

	free( ii );
	close( sock );
	return 0;
}
