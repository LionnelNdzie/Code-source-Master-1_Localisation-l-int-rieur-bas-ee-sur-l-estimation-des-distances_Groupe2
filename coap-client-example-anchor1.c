#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "rest.h"
#include "buffer.h"
#include "net/packetbuf.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "simple-udp.h"

#include "net/uip-debug.h"

#include "sys/node-id.h"

#include "simple-udp.h"
#include "servreg-hack.h"

#define UDP_PORT 1234
#define SERVICE_ID 190
#define ANCHOR_ID 1 // Value used to identify the rssi sent by the anchor

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])

#define PRINTLLADDR(lladdr) PRINTF(" %02x:%02x:%02x:%02x:%02x:%02x ",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xfe80, 0, 0, 0, 0xc30c, 0, 0, 0x0001)
//#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xfe80, 0, 0, 0, 0xc30c, 0, 0, 0xfcf)
#define LOCAL_PORT 61617
#define REMOTE_PORT 61616

/*---------------------------------------------------------------------------*/
char temp[100]; // The buffer
int xact_id;
static uip_ipaddr_t server_ipaddr; // The IP address of the COAP server
static uip_ipaddr_t *ipaddr; // The global ipv6 address of that node used for unicast packets
static struct uip_udp_conn *client_conn; // The udp connection with the COAP server

#define MAX_PAYLOAD_LEN   100

#define NUMBER_OF_URLS 1
char* service_urls[NUMBER_OF_URLS] = {"temperature"};

/* The connection used to receive RSSI_request from coordinator */
static struct simple_udp_connection multicast_connection;
/* The connection used to send RSSIs to coordinator */
static struct simple_udp_connection unicast_connection;
/* The unicast address of the coordinator */
uip_ipaddr_t *addr_u;

#define RSSI_SERVER_PORT 10000
#define RSSI_CLIENT_PORT 10001

unsigned long current_sending_time = 0;

/*---------------------------------------------------------------------------*/
static uip_ipaddr_t *
set_global_address(void)
{
  static uip_ipaddr_t ipaddr;
  int i;
  uint8_t state;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  printf("IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      printf("\n");
    }
  }

  return &ipaddr;
}

/*********************************************************************************/
static void
send_data(void)
{
	/* The anchor sends the coap request to the dumb node */
  char buf[MAX_PAYLOAD_LEN];

  if (init_buffer(COAP_DATA_BUFF_SIZE)) {
    int data_size = 0;
    int service_id = random_rand() % NUMBER_OF_URLS;
    coap_packet_t* request = (coap_packet_t*)allocate_buffer(sizeof(coap_packet_t));
    init_packet(request);

    coap_set_method(request, COAP_GET);
    request->tid = xact_id++;
    request->type = MESSAGE_TYPE_CON;
    coap_set_header_uri(request, service_urls[service_id]);

    data_size = serialize_packet(request, buf);

    PRINTF("Client sending coap request to:[");
    PRINT6ADDR(&client_conn->ripaddr);
    PRINTF("]:%u/%s\n", (uint16_t)REMOTE_PORT, service_urls[service_id]);
    uip_udp_packet_send(client_conn, buf, data_size);
    
    current_sending_time = 0;
    current_sending_time = clock_seconds();
    
    printf("Anchor%d sending time: %lu\n", ANCHOR_ID, current_sending_time);

    delete_buffer();
  }
	
}

/* The callback function used to manage multicast communication between the coordinator and the anchors */
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
	printf("Anchor%d sending the COAP request\n", ANCHOR_ID);
	/* We send the COAP request to the dumb node */
	send_data();
}

/* The callback function used to manage unicast communication with the coordinator */
static void
receiver1(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
}

/*-------------------------------------------------------------------*/
static void
response_handler(coap_packet_t* response)
{
  uint16_t payload_len = 0;
  uint8_t* payload = NULL;
  payload_len = coap_get_payload(response, &payload);

  if (payload) {
    memcpy(temp, payload, payload_len);
    temp[payload_len] = 0;
		printf("Reception time received : %lus\n", atol(temp));
	
		// Sends the data to the coordinator in unicast
		addr_u = servreg_hack_lookup(SERVICE_ID);
		if(addr_u != NULL) {
      char buf[32];

      printf("Sending Time_response to ");
      uip_debug_ipaddr_print(addr_u);
      printf("\n");
      
      sprintf(buf, "%d:%lu", ANCHOR_ID, (atol(temp) - current_sending_time));
      printf("sending %s to coord\n", buf);
      simple_udp_sendto(&unicast_connection, buf, strlen(buf) + 1, addr_u);
    } 
    else {
      printf("Service %d not found\n", SERVICE_ID);
    }
  }
}

static void
handle_incoming_data()
{
  PRINTF("Incoming packet size: %u \n", (uint16_t)uip_datalen());
	/* If the message received is different from "dbm:xxxx" */
	if (init_buffer(COAP_DATA_BUFF_SIZE)) {
	  if (uip_newdata()) {
	    coap_packet_t* response = (coap_packet_t*)allocate_buffer(sizeof(coap_packet_t));
			
	    if (response) {
	      parse_message(response, uip_appdata, uip_datalen());
	      response_handler(response);
	    }
	  }
	  delete_buffer();
	}
}

/**
 * The Start of the main program 
 */

PROCESS(main_process, "The main process");
PROCESS(multicast_with_coordinator, "The process for multicast with coordinator");

AUTOSTART_PROCESSES(&main_process, &main_process, &multicast_with_coordinator);

PROCESS_THREAD(main_process, ev, data)
{
  PROCESS_BEGIN();
  
  clock_init();

	servreg_hack_init();

  ipaddr = set_global_address();

	simple_udp_register(&unicast_connection, UDP_PORT, NULL, UDP_PORT, receiver1);

  SERVER_NODE(&server_ipaddr);

  /* new connection with server */
  client_conn = udp_new(&server_ipaddr, UIP_HTONS(REMOTE_PORT), NULL);
  udp_bind(client_conn, UIP_HTONS(LOCAL_PORT));

  PRINTF("Created a connection with the coap server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
  UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

	while(1) {
  	PROCESS_YIELD();
		if (ev == tcpip_event) {
    	handle_incoming_data();
  	}
	}

  PROCESS_END();
}

PROCESS_THREAD(multicast_with_coordinator, ev, data)
{
  PROCESS_BEGIN();

	/* Prepare multicast with the coordinator */
	simple_udp_register(&multicast_connection, RSSI_SERVER_PORT, NULL, RSSI_CLIENT_PORT, receiver);

	while(1) {
  	PROCESS_WAIT_EVENT();
	}

  PROCESS_END();
}
