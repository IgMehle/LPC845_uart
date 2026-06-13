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
#include "my_uart/my_uart.h"
/* TODO: insert other definitions and declarations here. */
#define UART0_TX_PIN	25
#define UART0_RX_PIN	24
//#define UART1_TX_PIN	7
//#define UART1_RX_PIN	12

/*
 * @brief   Application entry point.
 */
int main(void) {
	// FRO init
	BOARD_BootClockFRO24M();
	// NO INICIALIZO DEBUG CONSOLE SI USO UART EN PINES 24 Y 25

	// Mapeo pines TX y RX de UART0
	CLOCK_EnableClock(kCLOCK_Swm);
	SWM_SetMovablePinSelect(SWM0, kSWM_USART0_TXD, UART0_TX_PIN);
	SWM_SetMovablePinSelect(SWM0, kSWM_USART0_RXD, UART0_RX_PIN);
	CLOCK_DisableClock(kCLOCK_Swm);
	CLOCK_Select(kUART0_Clk_From_MainClk);

	// UART0 INIT
	uart_init(USART0, 9600U);

	char c = 0;
	uint8_t i = 0;
	char bf[UART_BUFFER_SIZE];
	char prompt[] = ">> ";
	uart_write(USART0, prompt);

	// Habilito IRQ de RX
	USART_EnableInterrupts(USART0, kUSART_RxReadyInterruptEnable);
	NVIC_EnableIRQ(USART0_IRQn);

	while (1) {
//		if (rx_echo[0] != '\0') {
//			uart0_write(rx_echo);
//			rx_echo[0] = '\0';
//		}
		// Chequeo si termine de recibir una linea
		if (uart_new_line()) {
			// terminacion de linea CRLF
			uart_write(USART0, "\r\n");

			// Copio rx_buffer a buffer local
			i = 0;
			// lectura inicial
			c = uart_getc();
			while (c) {
				// copio caracter
				bf[i] = c;
				i++;
				// vuelvo a leer
				c = uart_getc();
			}
			// aseguro terminacion de string
			bf[i] = '\0';

			if (bf[0] != '\0') {
				// Comparo y escribo en uart
				if (strcmp(bf, "ping") == 0) {
					uart_write(USART0, "< PONG\r\n");
				}
				else uart_write(USART0, "< NACK\r\n");
			}
			// escribo prompt
			uart_write(USART0, prompt);
		}
	}
    return 0;
}


