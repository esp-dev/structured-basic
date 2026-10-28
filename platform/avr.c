#include "sbasic.h"
#include <avr/io.h>
#include <avr/pgmspace.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif
static SBasic basic;
static const char program[] PROGMEM =
    "PRINT FACT(6)\n"
    "FUNCTION FACT(N)\n"
    "IF N <= 1 THEN\nRETURN 1\nEND IF\n"
    "RETURN N * FACT(N-1)\nEND FUNCTION\nEND\n";
/* Debugger-visible result: 0 on completion, otherwise SBError. */
volatile uint8_t firmware_status;
static void uart_byte(char ch) {
    while (!(UCSR0A & _BV(UDRE0))) { }
    UDR0 = (uint8_t)ch;
}
static int flash_read(void *user, uint32_t offset) {
    (void)user; return pgm_read_byte(program + (uint16_t)offset);
}
static int uart_print(void *user, int32_t value) {
    char digits[10];
    uint8_t count = 0;
    uint32_t magnitude;
    (void)user;
    if (value < 0) { uart_byte('-'); magnitude = 0u - (uint32_t)value; }
    else magnitude = (uint32_t)value;
    do { digits[count++] = (char)('0' + magnitude % 10u); magnitude /= 10u; }
    while (magnitude);
    while (count) uart_byte(digits[--count]);
    uart_byte('\r'); uart_byte('\n'); return 0;
}
int main(void) {
    SBSource source;
    uint16_t divisor = (uint16_t)(F_CPU / 16UL / 9600UL - 1UL);
    UBRR0H = (uint8_t)(divisor >> 8); UBRR0L = (uint8_t)divisor;
    UCSR0B = _BV(TXEN0); UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    source.read = flash_read; source.user = NULL; source.length = sizeof(program) - 1u;
    firmware_status = (uint8_t)sb_compile(&basic, source);
    if (!firmware_status)
        firmware_status = (uint8_t)sb_run(&basic, uart_print, NULL, UINT32_C(100000));
    /* No interrupts are enabled; attach UART0 at 9600 8N1 to observe 720. */
    for (;;) { }
}
