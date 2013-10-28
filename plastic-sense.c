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


#define DEBUG

#ifdef DEBUG
#define INFO(...) fprintf(stderr, __VA_ARGS__)
#else
#define INFO(...) 
#endif

#define OPCODE_SET_MAC 1
#define OPCODE_SET_IP (OPCODE_SET_MAC+1)
#define OPCODE_SET_DNS 3
#define OPCODE_SET_SUBNET 4
#define OPCODE_SET_GATEWAY 5
#define OPCODE_SET_DHCP_ON 6
#define OPCODE_RENEW_DHCP_LEASE 7
#define OPCODE_GET_IP 8
#define OPCODE_GET_DNS 9
#define OPCODE_GET_SUBNET 10
#define OPCODE_GET_GATEWAY 11

#define OPCODE_CONNECT_TO_HOST 12
#define OPCODE_CONNECT_TO_IP 13
#define OPCODE_TCP_WRITE 14
#define OPCODE_TCP_AVAILABLE 15
#define OPCODE_TCP_READ 16
#define OPCODE_TCP_PEEK 17
#define OPCODE_TCP_ALIVE 18

#define OPCODE_TCP_SERVER_START 19
#define OPCODE_GET_TCP_SERVER_CONNECTIONS 20
#define OPCODE_TCP_SERVER_WRITE 21


#define OPCODE_CALLBACK 22

//#define INTERVAL (CLOCK_SECOND/2)
#define SERVER_NOT_CONNECTED 0
#define SERVER_CONNECTED 1
#define CLIENT_NOT_CONNECTED 2
#define CLIENT_CONNECTED 3

#define STATE_FREE 0
#define STATE_IDLE 1
#define STATE_RECEIVING_TCP_DATA 2
#define STATE_ARDUINO_CALLBACK_TIMED_OUT 3


#define TCP_RX_BUFFER_SIZE 128
#define TCP_TX_BUFFER_SIZE 128

#define SERIAL_RX_BUFFER_SIZE 128 // need size of power two!

#define PACKET_TIMEOUT 60 * CLOCK_SECOND
#define MAX_CONNECTIONS 16
#define MAX_SERVER 4
/*---------------------------------------------------------------------------*/
uint8_t serialRx[SERIAL_RX_BUFFER_SIZE];
struct ringbuf serialRx_Buffer;

uint8_t transmissionTx_Buffer[TCP_TX_BUFFER_SIZE];
uint8_t transmissionTx_Pointer = 0;

uint8_t transmissionRx_Buffer[TCP_RX_BUFFER_SIZE];
uint8_t transmissionRx_Pointer = 0;



struct arduinoServer {
	uint16_t localPort;
};

struct connection {
	uint8_t state;

	uip_ipaddr_t ripaddr;
	uint16_t rport;
	uint16_t lport;

	uint8_t tcpRx_Buffer[TCP_RX_BUFFER_SIZE];
	struct ringbuf tcpRx_ringBuffer;
	
	uint8_t tcpTx_Buffer[TCP_TX_BUFFER_SIZE];
	uint8_t tcpTx_Pointer;
	
	struct psock ps_in, ps_out;
};

// FunDefs
struct connection *getConnectionFromRIpRPort(); // TODO check
void decodeSerialCommand(void *state);
void callback(uint8_t d);
void clearConnection(struct connection *c);
void tcp_appcall(void *state);
void serial_appcall(void *state);
void callArduino(struct connection *c);

// Serial RPC Vars
static uint8_t payloadLength = 255;
static int8_t opCode = -1;

struct connection connections[MAX_CONNECTIONS];                                     
struct arduinoServer arduinoServers[MAX_SERVER];    
/*---------------------------------------------------------------------------*/
static 
PT_THREAD(handle_input(struct connection *c))
{
	INFO("handle_input\n");
	struct psock *p = &c->ps_in;
	

	PSOCK_BEGIN(p);

	clock_time_t tcpReceiveStartTime = clock_time();
	do{
		// PSOCK_READBUF breaks our thread if there isn't
		// more to read.
		PSOCK_READBUF(p);
		
		transmissionRx_Pointer = PSOCK_DATALEN(p);
		
		// get enough space in rx buffer by removing the oldest entries.
		while((TCP_RX_BUFFER_SIZE - ringbuf_elements(&c->tcpRx_ringBuffer)) < transmissionRx_Pointer){
			ringbuf_get(&c->tcpRx_ringBuffer);
		}

		// fill rx buffer from transmission buffer
		for(uint8_t i = 0; i  < transmissionRx_Pointer; i++){
			ringbuf_put(&c->tcpRx_ringBuffer, transmissionRx_Buffer[i]);
		}
		transmissionRx_Pointer = 0;

		// notify the Arduino
		callArduino(c);

		// if eveything read fits in our Rx Buffer continue,
		// our Arduino could fetch the Data later.
		// We dont need to halt the scheduler.
		if(uip_datalen() < TCP_RX_BUFFER_SIZE){
			return;
		}

		// give the Arduino some time to react on the callback
		while(clock_time() - tcpReceiveStartTime < 200){
			// check if Arduino asked for "returnCode" bytes.
			serial_appcall(c);
			
			// wait until the Arduino has emptied the ringbuf
			if(ringbuf_elements(&c->tcpRx_ringBuffer) == 0)
				break;
		}
	} while(1);
	
	
  	PSOCK_CLOSE(p);
	PSOCK_END(p);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_output(struct connection *c))
{
	struct psock *p = &c->ps_out;
	
	PSOCK_BEGIN(p);
	
	while(1){
		PSOCK_WAIT_UNTIL(p, c->tcpTx_Pointer > 0)
		PSOCK_SEND(p, c->tcpTx_Buffer, c->tcpTx_Pointer);
		c->tcpTx_Pointer = 0;
	}
	PSOCK_CLOSE(p);
	PSOCK_END(p);
}
/*---------------------------------------------------------------------------*/
static void
handle_connection(struct connection *c)
{
	INFO("handle_connection\n");

		
	handle_input(c);


	handle_output(c);
}
/*---------------------------------------------------------------------------*/
PROCESS(plastic_sense_process, "plastic sense process");
AUTOSTART_PROCESSES(&plastic_sense_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(plastic_sense_process, ev, data)
{
	// TODO initialize connections
  	PROCESS_BEGIN();
	
  	ringbuf_init(&serialRx_Buffer, serialRx, sizeof(serialRx));
	uart0_set_input(callback);
	

	while(1) {
		PROCESS_WAIT_EVENT();
		INFO("RUN Plastic Sense Process\n");
		if(ev == tcpip_event){
			tcp_appcall(data);
		}
		else if (ev == PROCESS_EVENT_POLL){
			//handle Serial callback
			serial_appcall(data);
		}
	}

	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
// Handle input on the serial line.
void callback(uint8_t d){
         ringbuf_put(&serialRx_Buffer, d);
         process_poll(&plastic_sense_process);
}
/*---------------------------------------------------------------------------*/
void tcp_appcall(void *state){
	struct connection *c = (struct connection *)state;
	
	INFO("tcp_appcall\n");
	if(uip_closed() || uip_aborted() || uip_timedout()) {
		INFO("connection lost.\n");
		if(c != NULL) {
			// free Connection c
			clearConnection(c);
		}
	} else if(uip_connected()){ // ?! -> && c == NULL) {
	
		INFO("configuring new connection.\n");
		int i = 0;
		// Add new Connection (= mark one free Connection as non-free)
		for(i = 0; i < MAX_CONNECTIONS; i++) {
			if(connections[i].state == STATE_FREE){
				connections[i].state = STATE_IDLE;
				connections[i].ripaddr = uip_conn->ripaddr;
				connections[i].lport = UIP_HTONS(uip_conn->lport);
				connections[i].rport = UIP_HTONS(uip_conn->rport);
				
				ringbuf_init(&connections[i].tcpRx_ringBuffer, connections[i].tcpRx_Buffer, sizeof(connections[i].tcpRx_Buffer));

				c = &connections[i];
				break;
			}
		}
		if(i == MAX_CONNECTIONS) {
			//abort Connection if already MAX_CONNECTIONS are open.
			uip_abort();
			return;
		}
		tcp_markconn(uip_conn, c);
		
		PSOCK_INIT(&c->ps_in, (uint8_t *)transmissionRx_Buffer, sizeof(transmissionRx_Buffer) - 1);
		PSOCK_INIT(&c->ps_out, (uint8_t *)transmissionRx_Buffer, sizeof(transmissionRx_Buffer) - 1);
		
		handle_connection(c);
	} else if(c != NULL) {
		
		if(c->state == STATE_FREE){
			uip_abort();
			return;
		}
		INFO("handling connection\n");
		handle_connection(c);
	} else {	
		INFO("SHOULD NOT HAPPEN\n");
		uip_abort();
	}
}
/*---------------------------------------------------------------------------*/
void clearConnection(struct connection *c){
	c->state = STATE_FREE;
	c->tcpTx_Pointer = 0;
	c->rport = 0;
	c->lport = 0;
}
/*---------------------------------------------------------------------------*/
// Called if some Data arrived via UART or too much to buffer TCP Data is held in uip_buf
void serial_appcall(void *state){
	// timeout


	INFO("called: serial_appcall() \n");
		
	if(ringbuf_elements(&serialRx_Buffer) > 0 && opCode == -1){
		opCode = ringbuf_get(&serialRx_Buffer);
		INFO("set opCode to: %d \n", opCode);
	}
	if(ringbuf_elements(&serialRx_Buffer) > 0 && opCode > -1 && payloadLength == 255){
		payloadLength = ringbuf_get(&serialRx_Buffer);
		INFO("set payloadLength to: %d \n", payloadLength);
	}
	
	INFO("elements in ringbuf: %d \n", ringbuf_elements(&serialRx_Buffer));


	if(ringbuf_elements(&serialRx_Buffer) == payloadLength && payloadLength < 255){
		INFO("call: decodeSerialCommand() \n");
		decodeSerialCommand(state);
	}



}


/*---------------------------------------------------------------------------*/

void decodeSerialCommand(void *state){
	switch(opCode) {
		case OPCODE_SET_MAC: {
			INFO("decode: OPCODE_SET_MAC\n");
			// TODO
		break;
		}
		case OPCODE_SET_IP: {
			INFO("decode: OPCODE_SET_IP\n");
			// TODO
			// uip_ipaddr_t addr;
			// uip_ipaddr(&addr, 192,168,1,2);
			// uip_sethostaddr(&addr);
		break;
		}
		case OPCODE_SET_DNS: {
			// TODO
		break;
		}
		case OPCODE_SET_SUBNET: {
			// TODO
		break;
		}
		case OPCODE_SET_GATEWAY: {
			// TODO
		break;
		}
		case OPCODE_SET_DHCP_ON: {
			// TODO
		break;
		}
		case OPCODE_RENEW_DHCP_LEASE: {
			// TODO
		break;
		}
		case OPCODE_GET_IP: {
			// TODO
		break;
		}
		case OPCODE_GET_DNS: {
			// TODO
		break;
		}
		case OPCODE_GET_SUBNET: {
			// TODO
		break;
		}
		case OPCODE_GET_GATEWAY: {
			// TODO
		break;
		}
		case OPCODE_CONNECT_TO_HOST: {
			// TODO
		break;
		}
		case OPCODE_CONNECT_TO_IP: {
			INFO("decode: OPCODE_CONNECT_TO_IP\n");
			uint16_t ip[8];
			uint8_t i = 0;
			INFO("IP: ");
			for( i = 0; i < 8; i++){
				ip[i] = ringbuf_get(&serialRx_Buffer);
				ip[i] = ip[i] << 8;
				ip[i] = ip[i] | ringbuf_get(&serialRx_Buffer);

				INFO("%x:", ip[i]);
			}
			INFO("\n");
			uint16_t port;
			port = ringbuf_get(&serialRx_Buffer) << 8;
			port = ringbuf_get(&serialRx_Buffer) | port;
			INFO("Port: %d\n", port);
			
			uart0_writeb(OPCODE_CONNECT_TO_IP);
			uart0_writeb(0x01);
			
			uip_ipaddr_t ipaddr;
			uip_ip6addr(&ipaddr, ip[0],ip[1],ip[2],ip[3],ip[4],ip[5],ip[6],ip[7]);

			if(tcp_connect(&ipaddr, UIP_HTONS(port), NULL) !=  NULL){
				uart0_writeb(0x01); 
			} else{
				uart0_writeb(0x00);
			}
		break;
		}
		case OPCODE_TCP_WRITE: {
			INFO("decode: OPCODE_TCP_WRITE\n");
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();
			
			if(c == NULL){
				// TODO Handle Failue in Jennic and Arduino
				INFO("ERROR: COULD NOT WRITE, BECAUSE CONNECTION DOESNT EXIST ANYMORE\n");
				break;
			}

			uint8_t i = 0;
			for (i = 0; i < payloadLength; i++){
				c->tcpTx_Buffer[c->tcpTx_Pointer++] = ringbuf_get(&serialRx_Buffer);
			}
			uart0_writeb(OPCODE_TCP_WRITE);
			uart0_writeb(0x00);
		break;
		}
		case OPCODE_TCP_AVAILABLE: {
			INFO("decode: OPCODE_TCP_AVAILABLE\n");
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			if(c == NULL){
				// TODO Handle Failue in Jennic and Arduino
				INFO("ERROR: COULD NOT CHECK FOR AVAILABLE BYTES, BECAUSE CONNECTION DOESNT EXIST ANYMORE\n");
				break;
			}

			uart0_writeb(OPCODE_TCP_AVAILABLE);
			uart0_writeb(0x01);
			uart0_writeb(ringbuf_elements(&c->tcpRx_ringBuffer));
		break;
		}
		case OPCODE_TCP_READ: {

			INFO("decode: OPCODE_TCP_READ\n");
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			if(c == NULL){
				// TODO Handle Failure in Jennic and Arduino
				INFO("ERROR: COULD NOT READ, BECAUSE CONNECTION DOESNT EXIST ANYMORE\n");
				break;
			}
			
			
			uint8_t requestedAmountOfData = ringbuf_get(&serialRx_Buffer);

			// send opcode
			uart0_writeb(OPCODE_TCP_READ);
			INFO("uart opcode: %d\n", OPCODE_TCP_READ);

			if(requestedAmountOfData > ringbuf_elements(&c->tcpRx_ringBuffer))
				requestedAmountOfData = ringbuf_elements(&c->tcpRx_ringBuffer);

			// send payloadlength
			uart0_writeb(requestedAmountOfData);
			INFO("uart tcp_read is returning %d bytes.\n", requestedAmountOfData);


			INFO("Data: ");
			for (uint8_t i = 0; i < requestedAmountOfData; i++){
				uint8_t t = ringbuf_get(&c->tcpRx_ringBuffer);
				INFO("%c_", t);
				uart0_writeb(t);
			}
			INFO("\n");
		

		break;
		}
		case OPCODE_TCP_PEEK: {
			INFO("decode: OPCODE_TCP_PEEK\n");
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			if(c == NULL){
				// TODO Handle Failue in Jennic and Arduino
				INFO("ERROR: COULD NOT PEEK BYTE, BECAUSE CONNECTION DOESNT EXIST ANYMORE\n");
				break;
			}
			

			uart0_writeb(OPCODE_TCP_PEEK);
			INFO("uart opcode: %d\n", OPCODE_TCP_PEEK);
			
			// Payload Length
			uart0_writeb(0x01);
			
			// write peeked byte (if there is no byte to peek we returne 0)
			uint8_t x = 0;
			struct ringbuf *r = &c->tcpRx_ringBuffer;
			if(((r->put_ptr - r->get_ptr) & r->mask) > 0){
				x = r->data[r->get_ptr];
			}
			uart0_writeb(x);
			INFO("peeked byte: %c_", x);
		break;
		}
		case OPCODE_TCP_ALIVE: {
			INFO("decode: OPCODE_TCP_ALIVE\n");
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			uart0_writeb(OPCODE_TCP_ALIVE);
			uart0_writeb(0x01);

			if(c == NULL){
				uart0_writeb(0x01);
			}
			else{
				uart0_writeb(0x00);
			}
		break;
		}
		case OPCODE_TCP_SERVER_START: {
			INFO("decode: OPCODE_TCP_SERVER_START\n");
		       	uint16_t port;
		        port = ringbuf_get(&serialRx_Buffer) << 8;
		        port = ringbuf_get(&serialRx_Buffer) | port;
	
			tcp_listen(UIP_HTONS(port));
	
			uart0_writeb(OPCODE_TCP_SERVER_START);
			uart0_writeb(0x00);
		break;
		}
		case OPCODE_GET_TCP_SERVER_CONNECTIONS: {
			INFO("decode: OPCODE_TCP_SERVER_CONNECTIONS");
		       	uint16_t port;
		        port = ringbuf_get(&serialRx_Buffer) << 8;
			port = ringbuf_get(&serialRx_Buffer) | port;
			
			uart0_writeb(OPCODE_GET_TCP_SERVER_CONNECTIONS);
			int8_t i = 0;
			int8_t numClients = 0;
			for(i = 0; i < MAX_CONNECTIONS; i++){
				if ( connections[i].lport == port )  numClients++;
			}
			// write Payload Length
			uart0_writeb(18 * numClients);

			i = 0;
			for(i = 0; i < MAX_CONNECTIONS; i++){
				if ( connections[i].lport == port ) {
					
					for ( i = sizeof(connections[i].ripaddr.u8); i >= 0; i--){
						uint8_t b = connections[i].ripaddr.u8[i];
						uart0_writeb(b);
					}
					uart0_writeb((uint8_t) connections[i].rport >> 8);
					uart0_writeb((uint8_t) connections[i].rport);
				}
			}
		break;
		}
		case OPCODE_TCP_SERVER_WRITE: {
			// TODO
			INFO("decode: OPCODE_TCP_SERVER_WRITE\n");
			
       			uint16_t port;
        		port = ringbuf_get(&serialRx_Buffer) << 8;
        		port = ringbuf_get(&serialRx_Buffer) | port;
        		
			uint8_t lastConnectionId = 255; // NOT overflow safe!
			uint8_t i = 0;
			for( i = 0; i < MAX_CONNECTIONS; i++){
				if(connections[i].lport == port){
					uint8_t o = 0;
					for (o = 0; o < payloadLength; o++){
						if(lastConnectionId == 255){
							connections[i].tcpTx_Buffer[connections[i].tcpTx_Pointer++] = ringbuf_get(&serialRx_Buffer);
						}
						else{
							connections[i].tcpTx_Buffer[connections[i].tcpTx_Pointer++] = connections[lastConnectionId].tcpTx_Buffer[connections[lastConnectionId].tcpTx_Pointer-payloadLength+o];

						}
					}
					lastConnectionId = i;
				}
			}
			uart0_writeb(OPCODE_TCP_WRITE);
			uart0_writeb(0x00);
		break;
		}
	}

	opCode = -1;
	payloadLength = 255;
}


/*---------------------------------------------------------------------------*/
struct connection *getConnectionFromRIpRPort(){
	uint8_t ip[sizeof(uip_ipaddr_t)];
        uint8_t i = 0;
	for( i = 0; i < sizeof(ip); i++){
      		ip[i] = ringbuf_get(&serialRx_Buffer);
		//fprintf(stderr, "%x", ip[i]);
        }
        uint16_t port;
        port = ringbuf_get(&serialRx_Buffer) << 8;
        port = ringbuf_get(&serialRx_Buffer) | port;

        struct connection *c = NULL;
        for( i = 0; i < MAX_CONNECTIONS; i++){
		if(memcmp(connections[i].ripaddr.u8, ip, sizeof(ip)) == 0 && connections[i].rport == port){ // TODO memcmp manpage nachsehen
			c = &connections[i];
		}
	}
	return c;
}
/*---------------------------------------------------------------------------*/
void callArduino(struct connection *c){
	INFO("callArduino() called\n");
	// Send Message to Arduino that some Data arrived via TCP
	uart0_writeb(OPCODE_CALLBACK);
	uart0_writeb(0x12); // 0x12 == 0d18 // set site according to ip Version
	for ( uint8_t i=0; i<sizeof(uip_ipaddr_t); i++) {
		uint8_t b = c->ripaddr.u8[i];
		uart0_writeb(b);
		INFO("%x ", b);
	}
	INFO("Callback for R-Port: %u\n", c->rport);
	uint8_t port_msb = c->rport >> 8;
	uint8_t port_lsb = c->rport;

	uart0_writeb(port_msb);
	uart0_writeb(port_lsb);
}
/*---------------------------------------------------------------------------*/

