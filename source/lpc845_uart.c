/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    LPC845_uart.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
/* TODO: insert other include files here. */
#include "fsl_swm.h"
#include "fsl_usart.h"
#include <string.h>
/* TODO: insert other definitions and declarations here. */
#define UART0_TX_PIN	25
#define UART0_RX_PIN	24
//#define UART1_TX_PIN	7
//#define UART1_RX_PIN	12

#define BUFFER_SIZE		64
#define RING_BF_MASK	(BUFFER_SIZE - 1)

typedef struct {
	char bf[BUFFER_SIZE];
	uint8_t head;
	uint8_t tail;
	//uint8_t count;
} ring_buffer_t;

void uart0_init(void);
void uart0_write(char *bf);
void buffer_push(volatile ring_buffer_t *rb, char c);
char buffer_pop(volatile ring_buffer_t *rb);
// Teraterm manda 0x08 (ASCII BS).
// Algunos terminales mandan 0x7F (DEL).
// Conviene manejar los dos:
uint8_t buffer_unpush(volatile ring_buffer_t *rb);

volatile ring_buffer_t rx_buffer;
volatile char rx_echo[4] = {0, 0, 0, 0};
volatile uint8_t flag_new_line = 0;

/*
 * @brief   Application entry point.
 */
int main(void) {
	// FRO init
	BOARD_BootClockFRO24M();
	// NO INICIALIZO DEBUG CONSOLE SI USO UART EN PINES 24 Y 25

	// UART0 INIT
	uart0_init();

	char bf[BUFFER_SIZE];
	char prompt[] = ">> ";
	uart0_write(prompt);

	// Habilito IRQ de RX
	USART_EnableInterrupts(USART0, kUSART_RxReadyInterruptEnable);
	NVIC_EnableIRQ(USART0_IRQn);

	while (1) {
		if (rx_echo[0] != '\0') {
			uart0_write(rx_echo);
			rx_echo[0] = '\0';
		}
		// Chequeo si termine de recibir una linea
		if (flag_new_line) {
			// Limpio flag
			flag_new_line = 0;
			// terminacion de linea CRLF
			uart0_write("\r\n");

			// Copio rx_buffer a buffer local
			uint8_t i = 0;
			// lectura inicial
			char c = buffer_pop(&rx_buffer);
			while (c) {
				// copio caracter
				bf[i] = c;
				i++;
				// vuelvo a leer
				c = buffer_pop(&rx_buffer);
			}
			// aseguro terminacion de string
			bf[i] = '\0';

			if (bf[0] != '\0') {
				// Comparo y escribo en uart
				if (strcmp(bf, "ping") == 0) {
					uart0_write("< PONG\r\n");
				}
				else uart0_write("< NACK\r\n");
			}
			// escribo prompt
			uart0_write(prompt);
		}
	}
    return 0;
}

void USART0_IRQHandler(void) {
	uint32_t flags = USART_GetStatusFlags(USART0);
	static uint8_t last_was_cr = 0;

	if (flags & kUSART_RxReady) {
		// Revisar errores ANTES de leer el dato
		uint32_t err = flags & (kUSART_FramErrorFlag |
		                        kUSART_ParityErrorFlag |
		                        kUSART_RxNoiseFlag);
		if (err) {
			USART_ClearStatusFlags(USART0, err); // W1C vía SDK
			(void)USART_ReadByte(USART0);        // descartar byte corrupto
		}
		else {
			// Leo byte recibido
			uint8_t c = USART_ReadByte(USART0);

			// Si llego caracter CR
			if (c == '\r') {
				last_was_cr = 1;
				// termino el string
				buffer_push(&rx_buffer, '\0');
				// levanto flag de nueva linea
				flag_new_line = 1;

			}
			// Si llego caracter LF
			else if (c == '\n') {
				// Si previamente llego un CR, es un CRLF
				// Ignoro el LF
				if (last_was_cr) last_was_cr = 0;
				else {
					// Es un LF solo
					buffer_push(&rx_buffer, '\0');
					// levanto flag de nueva linea
					flag_new_line = 1;
				}
			}
			// Si llego backspace
			else if (c == '\b' || c == 0x7F) {
				if (buffer_unpush(&rx_buffer)) {
					// habia algo: mandar secuencia de borrado
					rx_echo[0] = '\b';
					rx_echo[1] = ' ';
					rx_echo[2] = '\b';
					rx_echo[3] = '\0';
				}
				else {
					// buffer vacio: no echo
					rx_echo[0] = '\0';
				}
				last_was_cr = 0;
			}
			else {
				last_was_cr = 0;
				buffer_push(&rx_buffer, c);
				// escribo echo de caracter
				rx_echo[0] = c;
				rx_echo[1] = '\0';
			}

		}
	}
	SDK_ISR_EXIT_BARRIER;
}

void uart0_init(void)
{
	// Mapeo pines TX y RX de UART0
	CLOCK_EnableClock(kCLOCK_Swm);
	SWM_SetMovablePinSelect(SWM0, kSWM_USART0_TXD, UART0_TX_PIN);
	SWM_SetMovablePinSelect(SWM0, kSWM_USART0_RXD, UART0_RX_PIN);
	CLOCK_DisableClock(kCLOCK_Swm);
	CLOCK_Select(kUART0_Clk_From_MainClk);

	usart_config_t uart_config;
	USART_GetDefaultConfig(&uart_config);
	// Parametros de la UART0
	uart_config.baudRate_Bps  = 9600U;
	uart_config.parityMode    = kUSART_ParityDisabled;   // o kUSART_ParityEven/Odd
	uart_config.stopBitCount  = kUSART_OneStopBit;        // o kUSART_TwoStopBit
	uart_config.bitCountPerChar = kUSART_8BitsPerChar;
	uart_config.enableTx      = true;
	uart_config.enableRx      = true;
	// Init uart0
	USART_Init(USART0, &uart_config, CLOCK_GetFreq(kCLOCK_MainClk));
}

void uart0_write(char *ptr)
{
	while (*ptr != 0) {
		// Espera TXRDY y escribe - sin gap entre bytes
		while (!(USART_GetStatusFlags(USART0) & kUSART_TxReady));
		USART_WriteByte(USART0, *ptr);
		// Muevo puntero
		ptr++;
	}
	// TXIDLE una sola vez al final: garantiza que el último byte
	// salió completo antes de que la función retorne
	while (!(USART_GetStatusFlags(USART0) & kUSART_TxIdleFlag));
}

void buffer_push(volatile ring_buffer_t *rb, char c)
{
	// calculo la posicion del nuevo head
	uint8_t next_head = (rb->head + 1) & RING_BF_MASK;
	// uint8_t next_head = (rb->head + 1) % BUFFER_SIZE;
	// si next apunta a tail, el buffer esta lleno
	if (next_head == rb->tail) {
		// overflow
		return;
	}
	// copio caracter
	rb->bf[rb->head] = c;
	// actualizo head
	rb->head = next_head;
	return;
}

char buffer_pop(volatile ring_buffer_t *rb)
{
	// si la queue esta vacia retorno caracter nulo
	if (rb->tail == rb->head) return '\0';
	// leo caracter
	char c = rb->bf[rb->tail];
	// calculo nueva posicion de tail
	rb->tail = (rb->tail + 1) & RING_BF_MASK;
	//rb->tail = (rb->tail + 1) % BUFFER_SIZE;
	// retorno caracter leido
	return c;
}

// Retorna 1 si había algo para borrar, 0 si el buffer estaba vacío
uint8_t buffer_unpush(volatile ring_buffer_t *rb)
{
    if (rb->head == rb->tail) return 0;  // nada que borrar
    rb->head = (rb->head - 1) & RING_BF_MASK;
    // rb->head = (rb->head - 1) % BUFFER_SIZE;
    return 1;
}
