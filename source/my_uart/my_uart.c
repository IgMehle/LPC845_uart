/*
 * my_uart.c
 *
 *  Created on: 12 jun. 2026
 *      Author: Ignacio Mehle
 */

#include "my_uart.h"

// VARIABLES DE LA LIBRERIA
volatile ring_buffer_t rx_buffer;
volatile ring_buffer_t tx_buffer;
volatile uint8_t flag_new_line = 0;

void uart_init(uart_handle_t uart, uint32_t baudrate)
{
	usart_config_t config;
	USART_GetDefaultConfig(&config);

	// Parametros de la UART0
	config.baudRate_Bps  = baudrate;
	config.parityMode    = kUSART_ParityDisabled;   // o kUSART_ParityEven/Odd
	config.stopBitCount  = kUSART_OneStopBit;        // o kUSART_TwoStopBit
	config.bitCountPerChar = kUSART_8BitsPerChar;
	config.enableTx      = true;
	config.enableRx      = true;
	// Init uart
	USART_Init(uart, &config, CLOCK_GetFreq(kCLOCK_MainClk));
}

void uart_enable_irq(uart_handle_t uart, n_uart_t n)
{
	// Habilito IRQ de RX
	USART_EnableInterrupts(uart, kUSART_RxReadyInterruptEnable);
	switch(n) {
	case UART0:
		NVIC_EnableIRQ(USART0_IRQn);
		break;
	case UART1:
		NVIC_EnableIRQ(USART1_IRQn);
		break;
	case UART2:
		NVIC_EnableIRQ(USART2_IRQn);
		break;
	default:
		break;
	}
}

void USART0_IRQHandler(void) {
	uint32_t flags = USART_GetStatusFlags(USART0);
	static uint8_t last_was_cr = 0;
	static char rx_echo[4] = {0};
	static char c = 0;

	// RX
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
					uart_write(USART0, rx_echo);
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
				uart_write(USART0, rx_echo);
			}

		}
	}

	// TX
	if (flags & kUSART_TxReady) {
		// Hay caracter no nulo en el buffer?
		c = buffer_pop(&tx_buffer);
		// Si es valido lo envio
		if (c) USART_WriteByte(USART0, c);
		// Si no hay mas caracteres en el buffer apago la IRQ
		else USART_DisableInterrupts(USART0, kUSART_TxReadyInterruptEnable);
	}

	SDK_ISR_EXIT_BARRIER;
}

uint8_t inline uart_new_line(void)
{
	if (flag_new_line) {
		flag_new_line = 0;
		return 1;
	}
	else return 0;
}

uint8_t uart_getc(void)
{
	return buffer_pop(&rx_buffer);
}

void uart_write_blocking(uart_handle_t uart, char *ptr)
{
	while (*ptr != 0) {
		// Espera TXRDY y escribe - sin gap entre bytes
		while (!(USART_GetStatusFlags(uart) & kUSART_TxReady));
		USART_WriteByte(uart, *ptr);
		// Muevo puntero
		ptr++;
	}
	// TXIDLE una sola vez al final: garantiza que el último byte
	// salió completo antes de que la función retorne
	while (!(USART_GetStatusFlags(uart) & kUSART_TxIdleFlag));
}

void uart_write(uart_handle_t uart, char *ptr)
{
	while (*ptr != 0) {
		buffer_push(&tx_buffer, *ptr);
		ptr++;
	}
	// habilito TX IRQ
	USART_EnableInterrupts(uart, kUSART_TxReadyInterruptEnable);
}

void buffer_push(volatile ring_buffer_t *rb, char c)
{
	// calculo la posicion del nuevo head
	uint8_t next_head = (rb->head + 1) & RING_BF_MASK;
	// uint8_t next_head = (rb->head + 1) % UART_BUFFER_SIZE;
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
	//rb->tail = (rb->tail + 1) % UART_BUFFER_SIZE;
	// retorno caracter leido
	return c;
}

// Retorna 1 si había algo para borrar, 0 si el buffer estaba vacío
uint8_t buffer_unpush(volatile ring_buffer_t *rb)
{
    if (rb->head == rb->tail) return 0;  // nada que borrar
    rb->head = (rb->head - 1) & RING_BF_MASK;
    // rb->head = (rb->head - 1) % UART_BUFFER_SIZE;
    return 1;
}
