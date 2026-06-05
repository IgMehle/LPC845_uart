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
#define UART1_TX_PIN	7
#define UART1_RX_PIN	12

#define BUFFER_SIZE		64
#define RING_BF_MASK	(BUFFER_SIZE - 1)

typedef struct {
	uint8_t bf[BUFFER_SIZE];
	uint8_t head;
	uint8_t tail;
	uint8_t count;
} ring_buffer_t;

void uart0_init(void);
void uart1_init(void);
void uart0_write(uint8_t *bf);
void buffer_push(ring_buffer_t *rb, uint8_t byte);

volatile uint8_t rx_buffer[BUFFER_SIZE];
volatile uint8_t rx_index = 0;
volatile uint8_t rx_echo[2] = {0, 0};
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

	// Habilito IRQ de RX
	USART_EnableInterrupts(USART0, kUSART_RxReadyInterruptEnable);
	NVIC_EnableIRQ(USART0_IRQn);

	uint8_t *ptr, i = 0;
	uint8_t bf[BUFFER_SIZE];
	uint8_t resp[BUFFER_SIZE];

	while (1) {
		if (*rx_echo) {
			uart0_write(rx_echo);
			rx_echo[0] = 0;
		}
		// Chequeo si termine de recibir una linea
		if (flag_new_line) {
			// Limpio flag
			flag_new_line = 0;
			// Copio rx_buffer a buffer local
			USART_DisableInterrupts(USART0, kUSART_RxReadyInterruptEnable);
			memcpy(bf, (const uint8_t*)rx_buffer, rx_index + 1);
			memset((uint8_t*)rx_buffer, 0, rx_index + 1);
			rx_index = 0;
			USART_EnableInterrupts(USART0, kUSART_RxReadyInterruptEnable);
			// Comparo
			if (strncmp(bf, "ping", 4) == 0) {
				strcpy(resp, "PONG\n");
			}
			else strcpy(resp, "NACK\n");
			// escribo en uart
			uart0_write(resp);
		}
	}
    return 0;
}

void USART0_IRQHandler(void) {
	uint32_t flags = USART_GetStatusFlags(USART0);

	if (flags & kUSART_RxReady) {
		// Revisar errores ANTES de leer el dato
		uint32_t err = flags & (kUSART_FramErrorFlag |
		                        kUSART_ParityErrorFlag |
		                        kUSART_RxNoiseFlag);
		if (err) {
			USART_ClearStatusFlags(USART0, err); // W1C vía SDK
			(void)USART_ReadByte(USART0);        // descartar byte corrupto
		}
		else if (rx_index < BUFFER_SIZE - 1) {
			// Leo byte recibido
			uint8_t c = USART_ReadByte(USART0);
			rx_buffer[rx_index] = c;
			rx_index++;
			// aseguro terminacion de string
			rx_buffer[rx_index] = 0;
			// Si llego el caracter de nueva linea levanto el flag
			if (c == '\n') {
				flag_new_line = 1;
			}
			rx_echo[0] = c;
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

void uart0_write(uint8_t *ptr)
{
	while (*ptr != 0) {
		// Espera TXRDY y escribe
		while (!(USART_GetStatusFlags(USART0) & kUSART_TxReady));
		USART_WriteByte(USART0, *ptr);
		// Muevo puntero
		ptr++;
		// Para esperar que el frame entero salió por el pin (TXIDLE):
		while (!(USART_GetStatusFlags(USART0) & kUSART_TxIdleFlag));
	}
}

void buffer_push(ring_buffer_t *rb, uint8_t byte)
{
	// calculo la posicion del nuevo head
	uint8_t next_head = (rb->head + 1) & RING_BF_MASK;
	// si next apunta a tail, el buffer esta lleno
	if (next_head == rb->tail) {
		// overflow
		return;
	}
	// copio byte
	rb->bf[rb->head] = byte;
	// actualizo head
	rb->head = next_head;
	return;
}
