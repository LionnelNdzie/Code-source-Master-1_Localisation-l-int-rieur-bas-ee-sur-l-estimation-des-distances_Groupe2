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
#include "net/uip-debug.h"
#include "simple-udp.h"
#include "lib/random.h"
#include "sys/etimer.h"

#include "servreg-hack.h"

#include "net/rpl/rpl.h"

#define UDP_PORT 1234
#define SERVICE_ID 190

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

#define TIME_FETCHERS 2

/**********************************************************************/
int all_rounds = 0; // Tracks the number of times the localization have been performed
char temp[100]; // The buffer
int xact_id;

static uip_ipaddr_t server_ipaddr; // The IP address of the COAP server
static uip_ipaddr_t addr; // The multicast address to which to send the RSSI_request
static uip_ipaddr_t *ipaddr; // The global ipv6 address of that node used for unicast packets

static struct uip_udp_conn *client_conn; // The udp connection with the COAP server
static struct etimer et; // The timer used for each round

char time_delays[TIME_FETCHERS][8]; // The array where to store the RSSIs and their respective senders
unsigned int sending_time = 0;
int time_delays_counter = 0; // This variable tracks the number of rssis packet correctly received so far

#define MAX_PAYLOAD_LEN   100

#define NUMBER_OF_URLS 1
char* service_urls[NUMBER_OF_URLS] = {"temperature"};

/* The connection used to request  RSSIs to anchors */
static struct simple_udp_connection multicast_connection;
/* The connection used to receive  RSSIs from anchors */
static struct simple_udp_connection unicast_connection;

#define RSSI_SERVER_PORT 10000
#define RSSI_CLIENT_PORT 10001


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

/*---------------------------------------------------------------------------*/
static void
create_rpl_dag(uip_ipaddr_t *ipaddr)
{
  struct uip_ds6_addr *root_if;

  root_if = uip_ds6_addr_lookup(ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;
    uip_ipaddr_t prefix;
    
    rpl_set_root(RPL_DEFAULT_INSTANCE, ipaddr);
    dag = rpl_get_any_dag();
    uip_ip6addr(&prefix, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
    rpl_set_prefix(dag, &prefix, 64);
    PRINTF("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }
}

/*---------------------------------------------------------------------------*/
/* The callback function used to manage a communication between the coordinator and the anchors */
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
}

/* Simple procedure to test if all required rssis have been received */
void 
test_time_delays() {
	if(time_delays_counter >= TIME_FETCHERS){
		int i = 0;
		printf("Python:");
		for(i = 0; i < time_delays_counter; i++) {
			printf("%s;", time_delays[i]);
		}
		
		for(i = 0; i < time_delays_counter; i++) {
			bzero(time_delays[i], 8);
		}
		
		printf("\n\n");
		time_delays_counter = 0;
	}
}

/* The callback function used to receive unicast packets from anchors */
static void
receiver1(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
	printf("Received on port %d from port %d value: %s\n", receiver_port, sender_port, data);
	bzero(time_delays[time_delays_counter], 8);
	sprintf(time_delays[time_delays_counter], "%s", data);
	time_delays_counter++;
	
	test_time_delays();
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
    
    PRINTF("COAP payload: %s\n", temp);
    
		printf("Received value of time of reception: %lus\n", atol(temp));
		
		// We store the time delay in the array
		bzero(time_delays[time_delays_counter], 8);
		sprintf(time_delays[time_delays_counter], "%d:%lu", 0, sending_time - atol(temp));

		time_delays_counter++;

		test_time_delays();
  }
}

/* Handle the coap response from the dumb node */
static void
handle_incoming_data()
{
  // PRINTF("Incoming packet size: %u \n", (uint16_t)uip_datalen());
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

/* Function used to send the coap request to the dumb node */
static void
send_data(void)
{
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

    PRINTF("Client sending request to:[");
    PRINT6ADDR(&client_conn->ripaddr);
    PRINTF("]:%u/%s\n", (uint16_t)REMOTE_PORT, service_urls[service_id]);
    
    uip_udp_packet_send(client_conn, buf, data_size);
    
    sending_time = 0;
    sending_time = clock_seconds();

    delete_buffer();
  }
	
}

/*
 * The start of the main program
 */
PROCESS(main_process, "The main Process");
PROCESS(unicast_with_anchors, "Handler of incomming unicast");

AUTOSTART_PROCESSES(&main_process, &unicast_with_anchors);

PROCESS_THREAD(main_process, ev, data)
{
  PROCESS_BEGIN();
  
  clock_init();

  SERVER_NODE(&server_ipaddr);

  /* new connection with server */
  client_conn = udp_new(&server_ipaddr, UIP_HTONS(REMOTE_PORT), NULL);
  udp_bind(client_conn, UIP_HTONS(LOCAL_PORT));

  PRINTF("Created a connection with the coap server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
  UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

	/* We prepare the coordinator to receive unicast packets */
	servreg_hack_init();

  ipaddr = set_global_address();

  create_rpl_dag(ipaddr);

  servreg_hack_register(SERVICE_ID, ipaddr);

	/* Prepare multicast with anchors */
	simple_udp_register(&multicast_connection, RSSI_CLIENT_PORT, NULL, RSSI_SERVER_PORT, receiver);

	/* We set the timer */
  etimer_set(&et, 5 * CLOCK_SECOND);

  while(1) {
    PROCESS_YIELD();
    if (etimer_expired(&et)) { // Every time the etimer has expired, it restart the task of quering rssis
			
			all_rounds++;
			
			// We initialize the rssi_counter
			time_delays_counter = 0;
			printf("ROUNDS SO FAR: %d\n", all_rounds);
			
			// Send the request to the coap server
      send_data();
      
      // Send the request to all the anchor nodes
			uip_create_linklocal_allnodes_mcast(&addr);
			printf("Sending Time_request multicast request\n");
  		simple_udp_sendto(&multicast_connection, "Time_request", 12, &addr);
    	
			etimer_reset(&et);
    } 
    else if (ev == tcpip_event) {
      handle_incoming_data(); // Every time there's a tcpip_event
    }
  }

  PROCESS_END();
}

PROCESS_THREAD(unicast_with_anchors, ev, data)
{
  PROCESS_BEGIN();

	simple_udp_register(&unicast_connection, UDP_PORT, NULL, UDP_PORT, receiver1);
	
	while(1) {
    PROCESS_WAIT_EVENT();
  }
  
  PROCESS_END();
}

