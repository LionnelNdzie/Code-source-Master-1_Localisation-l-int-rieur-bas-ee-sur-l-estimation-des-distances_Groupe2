#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "rest.h"
#include "dev/leds.h"
#include "dev/i2cmaster.h"
#include "dev/tmp102.h"

#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-debug.h"

#include "sys/node-id.h"

#include "simple-udp.h"
#include "servreg-hack.h"

#define UDP_PORT 1234

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)

#define PRINT6ADDR(addr) PRINTF(" %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x ", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])

#define PRINTLLADDR(lladdr) PRINTF(" %02x:%02x:%02x:%02x:%02x:%02x ",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

static struct simple_udp_connection unicast_connection;
static uip_ipaddr_t *ipaddr; // The global ipv6 address of that node used for unicast packets
char char_ipaddr[17]; // Store the last 16th bits of the ipv6 address 

char temp[100];

RESOURCE(discover, METHOD_GET, ".well-known/core");
void
discover_handler(REQUEST* request, RESPONSE* response)
{
  char temp[100];
  int index = 0;
  index += sprintf(temp + index, "%s,", "</temperature>;n=\"Temperature\"");
  index += sprintf(temp + index, "%s,", "</led>;n=\"LedControl\"");

  rest_set_response_payload(response, (uint8_t*)temp, strlen(temp));
  rest_set_header_content_type(response, APPLICATION_LINK_FORMAT);
}

/*A simple getter example. Returns the reading from light sensor with a simple etag*/
RESOURCE(temperature, METHOD_GET, "temperature");
void
temperature_handler(REQUEST* request, RESPONSE* response)
{
	unsigned long reception_time = clock_seconds();
	
  int16_t tempint;
  uint16_t tempfrac;
  int16_t raw;
  uint16_t absraw;
  int16_t sign;
  char minus = ' ';

	sign = 1;

  raw = tmp102_read_temp_raw();
  absraw = raw;
  if(raw < 0) {		// Perform 2C's if sensor returned negative data
    absraw = (raw ^ 0xFFFF) + 1;
    sign = -1;
  }

  tempint = (absraw >> 8) * sign;
  tempfrac = ((absraw >> 4) % 16) * 625;	// Info in 1/10000 of degree
  minus = ((tempint == 0) & (sign == -1)) ? '-' : ' ';

	sprintf(temp, "%lu", reception_time);

  rest_set_header_content_type(response, TEXT_PLAIN);
  rest_set_response_payload(response, (uint8_t*)temp, strlen(temp));
}

/*A simple actuator example, depending on the color query parameter and post variable mode, corresponding led is activated or deactivated*/
RESOURCE(led, METHOD_POST | METHOD_PUT , "led");

void
led_handler(REQUEST* request, RESPONSE* response)
{
  char color[10];
  char mode[10];
  uint8_t led = 0;
  int success = 1;

  if (rest_get_query_variable(request, "color", color, 10)) {
    PRINTF("color %s\n", color);

    if (!strcmp(color,"red")) {
      led = LEDS_RED;
    } else if(!strcmp(color,"green")) {
      led = LEDS_GREEN;
    } else if ( !strcmp(color,"blue") ) {
      led = LEDS_BLUE;
    } else {
      success = 0;
    }
  } else {
    success = 0;
  }

  if (success && rest_get_post_variable(request, "mode", mode, 10)) {
    PRINTF("mode %s\n", mode);

    if (!strcmp(mode, "on")) {
      leds_on(led);
    } else if (!strcmp(mode, "off")) {
      leds_off(led);
    } else {
      success = 0;
    }
  } else {
    success = 0;
  }

  if (!success) {
    rest_set_response_status(response, BAD_REQUEST_400);
  }
}

/* The callback function used to manage unicast communication with the coordinator */
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

PROCESS(main_process, "The coap dumb node main process");
PROCESS(unicast_process, "Unicast handler with the coordinator");

AUTOSTART_PROCESSES(&main_process, &unicast_process);

PROCESS_THREAD(main_process, ev, data)
{
  PROCESS_BEGIN();
  
  clock_init();

	servreg_hack_init();

  ipaddr = set_global_address();

  simple_udp_register(&unicast_connection, UDP_PORT, NULL, UDP_PORT, receiver);

#ifdef WITH_COAP
  PRINTF("COAP Server\n");
#else
  PRINTF("HTTP Server\n");
#endif

  rest_init();

	rest_activate_resource(&resource_discover);

	tmp102_init();
	rest_activate_resource(&resource_temperature);

  rest_activate_resource(&resource_led);

  PROCESS_END();
}

PROCESS_THREAD(unicast_process, ev, data)
{
  PROCESS_BEGIN();

  simple_udp_register(&unicast_connection, UDP_PORT, NULL, UDP_PORT, receiver);

	while(1) {
		PROCESS_WAIT_EVENT();
	}
	
  PROCESS_END();
}
