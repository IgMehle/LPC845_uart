/*
 * my_uart.h
 *
 *  Created on: 12 jun. 2026
 *      Author: Ignacio Mehle
 */

#ifndef MY_UART_H_
#define MY_UART_H_

#include "LPC845.h"
#include "fsl_usart.h"

// BUFFER SIZE
#define UART_BUFFER_SIZE	64
#define RING_BF_MASK		(UART_BUFFER_SIZE - 1)

// UART HANDLE
typedef USART_Type*		uart_handle_t;

// N UARTS
typedef enum uarts {
	UART0,
	UART1,
	UART2
} n_uart_t;

// RING BUFFER
typedef struct {
	char bf[UART_BUFFER_SIZE];
	uint8_t head;
	uint8_t tail;
	//uint8_t count;
} ring_buffer_t;

// INIT
void uart_init(uart_handle_t uart, uint32_t baudrate);
void uart_enable_irq(uart_handle_t uart, n_uart_t n);

// READ
uint8_t uart_new_line(void);
uint8_t uart_getc(void);

// WRITE
void uart_write_blocking(uart_handle_t uart, char *bf);
void uart_write(uart_handle_t uart, char *bf);

// RING BUFFERS
void buffer_push(volatile ring_buffer_t *rb, char c);
char buffer_pop(volatile ring_buffer_t *rb);
// Teraterm manda 0x08 (ASCII BS).
// Algunos terminales mandan 0x7F (DEL).
// Conviene manejar los dos:
uint8_t buffer_unpush(volatile ring_buffer_t *rb);

#endif /* MY_UART_H_ */
