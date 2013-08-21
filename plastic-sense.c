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


#define PACKET_TIMEOUT 60 * CLOCK_SECOND
#define MAX_CONNECTIONS 16
#define MAX_SERVER 4
/*---------------------------------------------------------------------------*/
uint8_t serialRx[128]; // need size of power two!
struct ringbuf serialRx_Buffer;

int8_t transmissionBuffer[128];
uint8_t transmissionPointer = 0;



struct arduinoServer {
	uint16_t localPort;
};

struct connection {
	uint8_t state;

	uip_ipaddr_t ripaddr;
	uint16_t rport;
	uint16_t lport;

	// Psock Read Buffer
	uint8_t tcpRx_Buffer[TCP_RX_BUFFER_SIZE];
	uint8_t tcpRx_Pointer;
	
	uint8_t tcpTx_Buffer[TCP_TX_BUFFER_SIZE];
	uint8_t tcpTx_Pointer;
	
	struct psock ps_in, ps_out;
};

// FunDefs
struct *getConnectionFromRIpRPort(); // TODO check
void decodeSerialCommand();
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
	struct psock *p = &c->ps_in;
	
	PSOCK_BEGIN(p);
	
	while(1){
		PSOCK_WAIT_UNTIL(p, PSOCK_NEWDATA(p));
		
		// if something arrived notify the Arduino
		if(uip_len > 0){
			if(c->state == STATE_IDLE) callArduino(c);
		
		
			if(uip_len > (TCP_RX_BUFFER_SIZE - c->tcpRx_Pointer) && ! c->state == STATE_ARDUINO_CALLBACK_TIMED_OUT)
			{
				c->state = STATE_RECEIVING_TCP_DATA;
				serial_appcall(c);
			}
			else // wenn die Daten in den Puffer passen ODER der STATE_ARDUINO_CALLBACK_TIMED_OUT erreicht wurde
			{
				// memcpy( &dst[dstIdx], &src[srcIdx], numElementsToCopy * sizeof( Element ) );
				uint8_t len = uip_len;
				if(uip_len > TCP_RX_BUFFER_SIZE - c->tcpRx_Pointer){
					len = TCP_RX_BUFFER_SIZE - c->tcpRx_Pointer;
				}
				memcpy(&c->tcpRx_Buffer[c->tcpRx_Pointer], &uip_buf, len);
				c->tcpRx_Pointer = c->tcpRx_Pointer + len;

				c->state = STATE_IDLE;
			}
	
		}

			
	}
	
	
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
		if(c->tcpTx_Pointer > 0){
			PSOCK_SEND(p, c->tcpTx_Buffer, c->tcpTx_Pointer);
			c->tcpTx_Pointer = 0;
		}
	}
	PSOCK_CLOSE(p);
	PSOCK_END(p);
}
/*---------------------------------------------------------------------------*/
static void
handle_connection(struct connection *c)
{
	// while State == warte noch auf Daten auf einer TCP Verbindung
	// time_t myTime = clock_time(); 
	// watchdog_periodic() Wenn der Timeout über 1 Sekunde dauert (1000* Clock_Seconds).
	// Wenn Timeout dann aus der handle_connection rausspringen

	time_t tcpReceiveStartTime = clock_time();

	do{
		if(clock_time() - tcpReceiveStartTime > 500) c->state = STATE_ARDUINO_CALLBACK_TIMED_OUT;
		
		handle_input(c);

		watchdog_periodic(); // TODO check imports!
	} while (c->state == STATE_RECEIVING_TCP_DATA);

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

	uart0_set_input(callback);
  	ringbuf_init(&serialRx_Buffer, serialRx, sizeof(serialRx));
	
	
	while(1) {
		PROCESS_WAIT_EVENT();
		if(ev == tcpip_event){
			tcp_appcall(data);
		}
		else if (ev == PROCESS_EVENT_POLL){
			//handle Serial callback
			serial_appcall(data);
			// handle_output() mit der richtigen Verbindung aufrufen -> nicht notwendig da serial_appcall die Daten aus uip_buf versendet wenn die bereffende Verbindung noch in STATE_RECEIVING_TCP_DATA ist. 
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
	
	if(uip_closed() || uip_aborted() || uip_timedout()) {
		if(c != NULL) {
			// free Connection c
			clearConnection(c);
		}
	} else if(uip_connected()){ // ?! -> && c == NULL) {
	
		int i = 0;
		// Add new Connection (= mark one free Connection as non-free)
		for(i = 0; i < MAX_CONNECTIONS; i++) {
			if(connections[i].state == STATE_FREE){
				connections[i].state = STATE_IDLE;
				connections[i].ripaddr = uip_conn->ripaddr;
				connections[i].lport = uip_conn->lport;
				connections[i].rport = uip_conn->rport;

				c = &connections[i];
			}
		}
		if(i == MAX_CONNECTIONS) {
			//abort Connection if already MAX_CONNECTIONS are open.
			uip_abort();
			return;
		}
		tcp_markconn(uip_conn, c);
		PSOCK_INIT(&c->ps_in, (uint8_t *)c->tcpRx_Buffer, sizeof(c->tcpRx_Buffer) - 1);
		PSOCK_INIT(&c->ps_out, (uint8_t *)c->tcpRx_Buffer, sizeof(c->tcpRx_Buffer) - 1);
		
		handle_connection(c);
	} else if(c != NULL) {
		
		if(c->state == STATE_FREE){
			uip_abort();
			return;
		}
		
		handle_connection(c);
	} else {
		uip_abort();
	}
}
/*---------------------------------------------------------------------------*/
void clearConnection(struct connection *c){
	c->state = STATE_FREE;
	// TODO check
	c->tcpRx_Pointer = 0;
	c->tcpTx_Pointer = 0;
}
/*---------------------------------------------------------------------------*/
// Called if some Data arrived via UART or too much to buffer TCP Data is held in uip_buf
void serial_appcall(void *state){
	// timeout

	
		
	if(ringbuf_elements(&serialRx_Buffer) > 0 && opCode == -1){
		opCode = ringbuf_get(&serialRx_Buffer);
	}
	if(ringbuf_elements(&serialRx_Buffer) > 0 && opCode > -1 && payloadLength == 255){
		payloadLength = ringbuf_get(&serialRx_Buffer);
	}
	if(ringbuf_elements(&serialRx_Buffer) == payloadLength && payloadLength < 255){
		decodeSerialCommand();
	}


	return;

	// TODO Magic
	/*
	
	decode serial line commands
	 |-> add/remove connections
	 |-> Add something to the tcp_Tx Buffers
	 |-> Read something from the tcp_Rx Buffers
		-> check if any connection State is STATE_RECEIVING_TCP_DATA if so send Arduino the connection->tcpRx_Buffer PLUS the uip_buf (set the connection state to idle and the pointers to 0)
	 
	 */

}


/*---------------------------------------------------------------------------*/

void decodeSerialCommand(){
	switch(opCode) {
		case OPCODE_SET_MAC:
			// TODO
			// uint8_t mac_address[8] EEMEM = {0x02, 0x11, 0x22, 0xff, 0xfe, 0x33, 0x44, 0x55};
		break;
		case OPCODE_SET_IP:
			// uip_ipaddr_t addr;
			// uip_ipaddr(&addr, 192,168,1,2);
			// uip_sethostaddr(&addr);
		break;
		case OPCODE_CONNECT_TO_IP: {
			uint16_t ip[8];
			uint8_t i = 0;
			for( i = 0; i < 8; i++){
				ip[i] = ringbuf_get(&serialRx_Buffer);
				ip[i] = ip[i] << 8;
				ip[i] = ip[i] | ringbuf_get(&serialRx_Buffer);
			}
			uint16_t port;
			port = ringbuf_get(&serialRx_Buffer) << 8;
			port = ringbuf_get(&serialRx_Buffer) || port;
			
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
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			uint8_t i = 0;
			for (i = 0; i < payloadLength; i++){
				c->tcpTx_Buffer[c->tcpTx_Pointer++] = ringbuf_get(&serialRx_Buffer);
			}
			uart0_writeb(OPCODE_TCP_WRITE);
			uart0_writeb(0x00);
		break;
		}
		case OPCODE_TCP_AVAILABLE: {
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			uart0_writeb(OPCODE_TCP_AVAILABLE);
			uart0_writeb(0x01);
			if(c->STATE_RECEIVING_TCP_DATA){
				uart0_writeb(uip_len + c->tcpRx_Pointer);
			}else{
				uart0_writeb(c->tcpRx_Pointer);
			}
		break;
		}
		case OPCODE_TCP_READ: {
                        struct connection *c;
			c = (struct connection *) getConnectionFromRIpRPort();

			uart0_writeb(OPCODE_TCP_READ);
			if(c->STATE_RECEIVING_TCP_DATA){
				uart0_writeb(uip_len + c->tcpRx_Pointer);
			}else{
				uart0_writeb(c->tcpRx_Pointer);
			}
			
			uint8_t i = 0;
			for ( i = 0; i < c->tcpRx_Pointer; i++){
				uart0_writeb(c->tcpRx_Buffer[i]);
			}
			c->tcpRx_Pointer = 0;

			if(c->state == STATE_RECEIVING_TCP_DATA){
				for ( i = 0; i < uip_len; i++){
					uart0_writeb(uip_buf[i]);
				}
				c->state = STATE_IDLE;
			}
		break;
		}
		case OPCODE_TCP_SERVER_START: {
		       	uint16_t port;
		        port = ringbuf_get(&serialRx_Buffer) << 8;
		        port = ringbuf_get(&serialRx_Buffer) || port;
	
			tcp_listen(UIP_HTONS(port));
	
			uart0_writeb(OPCODE_TCP_SERVER_START);
			uart0_writeb(0x00);
		break;
		}
		case OPCODE_GET_TCP_SERVER_CONNECTIONS: {
		       	uint16_t port;
		        port = ringbuf_get(&serialRx_Buffer) << 8;
		        port = ringbuf_get(&serialRx_Buffer) || port;
			
			uart0_writeb(OPCODE_GET_TCP_SERVER_CONNECTIONS);
			uart0_writeb(0x12);

			int8_t i = 0;
			for(i = 0; i < MAX_CONNECTIONS; i++){
				if ( connections[i].lport == port ) {
					
					for ( i = 15; i >= 0; i--){
						uint8_t b = (uint8_t) (connections[i].ripaddr >> (i * 8));
						uart0_writeb(b);
					}
					uart0_writeb((uint8_t) connections[i].rport >> 8);
					uart0_writeb((uint8_t) connections[i].rport);
				}
			}
		break;
		}
	}

	opCode = -1;
	payloadLength = 255;
}


/*---------------------------------------------------------------------------*/
struct *getConnectionFromRIpRPort(){
	uint8_t ip[16];
        uint8_t i = 0;
	for( i = 0; i < 16; i++){
      		ip[i] = ringbuf_get(&serialRx_Buffer);
        }
        uint16_t port;
        port = ringbuf_get(&serialRx_Buffer) << 8;
        port = ringbuf_get(&serialRx_Buffer) || port;

        struct connection *c;
        for( i = 0; i < MAX_CONNECTIONS; i++){
		if(memcmp(connections[i]->ripaddr, ip, sizeof(ip)) == 0 && connections[i]->rport == port){ // TODO memcmp manpage nachsehen
						uart0_writeb(0x12);
						c = &connections[i];
		}
	return c;
}
/*---------------------------------------------------------------------------*/
void callArduino(struct connection *c){
	// Send Message to Arduino that some Data arrived via TCP
	uart0_writeb(OPCODE_CALLBACK);
	uart0_writeb(0x12); // 0x12 == 0d18
	uint8_t i = 0;
	for ( i = 15; i >= 0; i--){
		uint8_t b = (uint8_t) (c->ripaddr >> (i * 8))
		uart0_writeb(b);
	}
	uart0_writeb((uint8_t) c->rport >> 8);
	uart0_writeb((uint8_t) c->rport);
}
/*---------------------------------------------------------------------------*/

