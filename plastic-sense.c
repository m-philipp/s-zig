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


void callback(uint8_t d);
void clearConnection(struct connection *c);
void tcp_appcall(void *state);
void serial_appcall(void *state);
void callArduino(struct connection *c);

// Parser
void addConnection(uint16_t);
void removeConnection(void *n);

struct arduinoServer {
	uint16_t localPort;
};

struct connection {
	uint8_t state = STATE_FREE;
	unsigned char timer;
	// Psock Read Buffer
	uint8_t tcpRx_Buffer[TCP_RX_BUFFER_SIZE];
	uint8_t tcpRx_Pointer = 0;
	
	uint8_t tcpTx_Buffer[TCP_TX_BUFFER_SIZE];
	uint8_t tcpTx_Pointer = 0;
	
	struct psock ps_in, ps_out;
};


time_t tcpReceiveStartTime = 0;

struct connection connections[MAX_CONNECTIONS];                                     
struct arduinoServer arduinoServers[MAX_SERVER];    
/*---------------------------------------------------------------------------*/
static 
PT_THREAD(handle_input(struct connection *c))
{
	struct psock *p = c->ps_in;
	
	PSOCK_BEGIN(p);
	
	while(1){
		PSOCK_WAIT_UNTIL(p, PSOCK_NEWDATA(s));
		
		// if something arrived notify the Arduino
		if(uip_len > 0){
			callArduino(c);
		
		
			if(uip_len > (TCP_RX_BUFFER_SIZE - c->tcpRx_Pointer) && ! c->state == STATE_ARDUINO_CALLBACK_TIMED_OUT)
			{
				c->state = STATE_RECEIVING_TCP_DATA;
			}
			else
			{
				// memcpy( &dst[dstIdx], &src[srcIdx], numElementsToCopy * sizeof( Element ) );
				memcpy(c->tcpRx_Buffer[c->tcpRx_Pointer], uip_buf, uip_len);
				c->tcpRx_Pointer = c->tcpRX_Pointer + uip_len;

				c->state = STATE_IDLE;
			}
	
		}

		// uip_len > rxpuffer
		// dann über die serielle schnittstele Arduino Callback versenden.
		
		// Wenn der Arduino nach den Daten der Verbindung gerfragt hat direkt seriell raussenden.

		

			
	}
	
	
  	PSOCK_CLOSE(p);
	PSOCK_END(p);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_output(struct connection *c))
{
	struct psock *p = c->ps_out;
	
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
	// TODO while State == warte noch auf Daten auf einer TCP Verbindung
	// time_t myTime = clock_time(); 
	// watchdog_periodic() Wenn der Timeout über 1 Sekunde dauert (1000* Clock_Seconds).
	// Wenn Timeout dann aus der handle_connection rausspringen
	
	if(tcpReceiveStartTime == 0) tcpReceiveStartTime = clock_time();


	do{
		if(clock_time() - tcpReceiveStartTime > 500) c->state = STATE_ARDUINO_CALLBACK_TIMED_OUT;

		handle_input(c);
		handle_output(c);

		watchdog_periodic();
	} while (c->state == STATE_RECEIVING_TCP_DATA);

	tcpReceiveStartTime = 0;
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
			if(connections[i]->state == STATE_FREE){
				connections[i]->state = STATE_IDLE;
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
void serial_appcall(data){

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

void addConnection(uint8_t isServer, uint16_t port, uip_ipaddr_t addr){

	if(isServer == 1){
		tcp_listen(UIP_HTONS(port));
	}
	else{
		tcp_connect(addr, UIP_HTONS(port), NULL); // in Fehlerfall gibt tcp_connect 0 zurück
	}
	
}
/*---------------------------------------------------------------------------*/
void callArduino(struct connection *c){
	// Send Message to Arduino that some Data arrived via TCP
}
/*---------------------------------------------------------------------------*/

