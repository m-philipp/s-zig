/*
 * Copyright (c) 2013, TU Darmstadt.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */
 
 // uIP TCP/IP stack in Contiki: http://contiki.sourceforge.net/docs/2.6/a01793.html
 // Protosockets in Contiki: http://contiki.sourceforge.net/docs/2.6/a01689.html
 // Contiki Lists: http://contiki.sourceforge.net/docs/2.6/a00004.html#a3
 
 // psock-client: https://github.com/contiki-os/contiki/blob/master/doc/example-psock-client.c
 // psock-server: https://github.com/contiki-os/contiki/blob/master/doc/example-psock-server.c
 

#include "contiki.h"
#include "dev/leds.h"
#include "dev/uart0.h"
#include "contiki-net.h"
#include "ringbuf.h"

#include <string.h>

#include <stdio.h>
#include <stdbool.h>

//#define INTERVAL (CLOCK_SECOND/2)
#define SERVER_NOT_CONNECTED 0
#define SERVER_CONNECTED 1
#define CLIENT_NOT_CONNECTED 2
#define CLIENT_CONNECTED 3

/*---------------------------------------------------------------------------*/
uint8_t serialRx[128]; // need size of power two!
struct ringbuf serialRx_Buffer;

int8_t transmissionBuffer[128];
uint8_t transmissionPointer = 0;

bool connected = false;
static struct psock ps;
// PSOCK Input Buffer
static char psock_RxBuffer[8];

void callback(uint8_t d);

// Parser
void addConnection(uint16_t);
void removeConnection(void *n);


struct connection_struct {
	uint8_t tcpRx_Buffer[128];
	uint16_t port;
	uint8_t isServer;
	struct psock ps;
};

#define PACKET_TIMEOUT 60 * CLOCK_SECOND
#define MAX_CONNECTIONS 16

struct connection_struct connections[MAX_CONNECTIONS];                                     

/*---------------------------------------------------------------------------*/
static 
PT_THREAD(handle_connection(struct psock *p))
{
	PSOCK_BEGIN(p);
	PSOCK_READTO(p, '\n');
	PSOCK_SEND(p, transmissionBuffer, sizeof(transmissionBuffer));
	transmissionPointer = 0;
  	PSOCK_CLOSE(p);
	PSOCK_END(p);
}


/*---------------------------------------------------------------------------*/
PROCESS(plastic_sense_process, "plastic sense process");
AUTOSTART_PROCESSES(&plastic_sense_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(plastic_sense_process, ev, data)
{

  	PROCESS_BEGIN();

	uart0_set_input(callback);
  	ringbuf_init(&serialRx_Buffer, serialRx, sizeof(serialRx));
	
	
	tcp_listen(UIP_HTONS(1010));

  	leds_on(LEDS_ALL);

	while(1) {
	
		PROCESS_WAIT_EVENT();
		
		int i = 0;
		for(i = 0; i < MAX_CONNECTIONS; i++) {
		
			if(n->port == uip_conn){
			
				// TODO Handle Connection
			
			
			if(ev == tcpip_event && !connected){
				if(uip_connected()) {
					PSOCK_INIT(&ps, psock_RxBuffer, sizeof(psock_RxBuffer));
				}
				connected = true;
			}
			else if(ev == tcpip_event && connected){
				if(!(uip_aborted() || uip_closed() || uip_timedout())){
					handle_connection(&ps);
				}
				else{
					connected = false;
				}
			} else{
				while(ringbuf_elements(&serialRx_Buffer) && transmissionPointer < sizeof(transmissionBuffer)){
							transmissionBuffer[transmissionPointer]  = ringbuf_get(&serialRx_Buffer);
							transmissionPointer++;
				}
			}
			}
				
		}
	
	
	
	
		
		
  	}	

	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
void callback(uint8_t d){
         ringbuf_put(&serialRx_Buffer, d);
         process_poll(&plastic_sense_process);
}
/*---------------------------------------------------------------------------*/
void addConnection(uint8_t isServer, uint16_t port, uip_ipaddr_t addr){
	struct connection_list_struct *e;
	e = memb_alloc(&connection_mem);

	newElement->port = port;
	newElement->isServer = isServer;
	
	list_add(connection_list, e);
	
	if(isServer != 1) tcp_listen(UIP_HTONS(port));
	else tcp_connect(addr, UIP_HTONS(port), NULL);
	

}
void removeConnection(void *n)
{
  struct connection_list_struct *e = n;
  list_remove(connection_list, e);
  memb_free(&connection_mem, e);
  
  // TODO close port and Stop Protothread
}

