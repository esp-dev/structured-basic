#include "sb_terminal.h"
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <string.h>
#include <util/atomic.h>
#ifndef F_CPU
#define F_CPU 16000000UL
#endif
#ifndef SB_AVR_SOURCE_MAX
#define SB_AVR_SOURCE_MAX 1536u
#endif
static SBasic basic;
static uint8_t source[SB_AVR_SOURCE_MAX], queued[16], head, tail, skip_lf;
static volatile uint32_t ticks;
volatile uint8_t firmware_status;
ISR(TIMER0_COMPA_vect) {
    ticks = (ticks + 1u) & UINT32_C(0x7fffffff);
}
static uint32_t millis(void) {
    uint32_t n;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        n = ticks;
    }
    return n;
}
static int uart_byte(uint8_t ch) {
    uint16_t timeout = 65535u;
    while (!(UCSR0A & _BV(UDRE0)))
        if (!--timeout)
            return -1;
    UDR0 = ch;
    return 0;
}
static int output(void *user, const uint8_t *p, uint16_t n) {
    uint16_t i;
    (void)user;
    for (i = 0; i < n; ++i) {
        if (p[i] == '\n' && uart_byte('\r'))
            return -1;
        if (uart_byte(p[i]))
            return -1;
    }
    return 0;
}
static int poll(void *user) {
    (void)user;
    while (UCSR0A & _BV(RXC0)) {
        uint8_t ch = UDR0, next = (uint8_t)((head + 1u) & 15u);
        if (ch == 3) {
            head = tail = 0;
            return 1;
        }
        if (next != tail) {
            queued[head] = ch;
            head = next;
        }
    }
    return 0;
}
static int input(void *user, uint8_t *p, uint16_t capacity, uint16_t *length) {
    int overflow = 0;
    (void)user;
    *length = 0;
    for (;;) {
        uint8_t ch;
        if (poll(NULL))
            return -2;
        if (head == tail)
            continue;
        ch = queued[tail];
        tail = (uint8_t)((tail + 1u) & 15u);
        if (ch == '\n' && skip_lf) {
            skip_lf = 0;
            continue;
        }
        skip_lf = 0;
        if (ch == '\r' || ch == '\n') {
            skip_lf = (uint8_t)(ch == '\r');
            uart_byte('\r');
            uart_byte('\n');
            return overflow ? -1 : 0;
        }
        if ((ch == 8 || ch == 127) && *length) {
            --*length;
            continue;
        }
        if (*length < capacity) {
            p[(*length)++] = ch;
            uart_byte(ch);
        } else
            overflow = 1;
    }
}
static int nvread(void *user, uint16_t address, uint8_t *p, uint16_t n) {
    uint16_t i;
    (void)user;
    if (address > 4096u || n > 4096u - address)
        return -1;
    for (i = 0; i < n; ++i)
        p[i] = eeprom_read_byte((const uint8_t *)(uintptr_t)(address + i));
    return 0;
}
static int nvwrite(void *user, uint16_t address, const uint8_t *p, uint16_t n) {
    uint16_t i;
    (void)user;
    if (address > 4096u || n > 4096u - address)
        return -1;
    for (i = 0; i < n; ++i) {
        uint16_t timeout = 65535u;
        while (!eeprom_is_ready())
            if (!--timeout)
                return -1;
        eeprom_update_byte((uint8_t *)(uintptr_t)(address + i), p[i]);
    }
    return 0;
}
static volatile uint8_t *reg(uint8_t port, uint8_t kind) {
    switch (port) {
    case 0:
        return kind == 0 ? &DDRA : kind == 1 ? &PORTA : &PINA;
    case 1:
        return kind == 0 ? &DDRB : kind == 1 ? &PORTB : &PINB;
    case 2:
        return kind == 0 ? &DDRC : kind == 1 ? &PORTC : &PINC;
    default:
        return kind == 0 ? &DDRD : kind == 1 ? &PORTD : &PIND;
    }
}
static int device(void *user, uint8_t op, int32_t a, int32_t b, int32_t *r) {
    uint8_t byte, mask, port;
    (void)user;
    *r = 0;
    if (op == SB_EEREAD || op == SB_EEWRITE) {
        if (a < 0 || a >= 4096 || (op == SB_EEWRITE && (b < 0 || b > 255)))
            return -1;
        if (op == SB_EEREAD) {
            if (nvread(NULL, (uint16_t)a, &byte, 1))
                return -1;
            *r = byte;
            return 0;
        }
        byte = (uint8_t)b;
        return nvwrite(NULL, (uint16_t)a, &byte, 1);
    }
    if (op == SB_MILLIS) {
        *r = (int32_t)millis();
        return 0;
    }
    if (op == SB_DELAY) {
        uint32_t begin = millis(), loops = UINT32_C(500000);
        if (a < 0 || a > 10)
            return -1;
        while (((millis() + UINT32_C(0x80000000) - begin) & UINT32_C(0x7fffffff)) < (uint32_t)a) {
            if (!--loops)
                return -1;
        }
        return 0;
    }
    if (op == SB_ADC) {
        uint16_t timeout = 65535u;
        if (a < 0 || a > 7)
            return -1;
        ADMUX = (uint8_t)(_BV(REFS0) | (uint8_t)a);
        ADCSRA = (uint8_t)(_BV(ADEN) | 7u | _BV(ADSC));
        while (ADCSRA & _BV(ADSC))
            if (!--timeout)
                return -1;
        *r = ADC;
        return 0;
    }
    if (a < 0 || a > 31 || a == 24 || a == 25)
        return -1;
    port = (uint8_t)((uint32_t)a / 8u);
    mask = (uint8_t)(1u << ((uint8_t)a & 7u));
    if (op == SB_PINREAD)
        *r = (*reg(port, 2) & mask) != 0;
    else if (op == SB_PINMODE || op == SB_PINWRITE) {
        volatile uint8_t *p = reg(port, op == SB_PINMODE ? 0 : 1);
        if (b < 0 || b > 1 || (op == SB_PINWRITE && !(*reg(port, 0) & mask)))
            return -1;
        if (b)
            *p |= mask;
        else
            *p &= (uint8_t)~mask;
    } else
        return -1;
    return 0;
}
int main(void) {
    SBPlatform p;
    SBTerminal terminal;
    uint16_t divisor = (uint16_t)(F_CPU / 16UL / 9600UL - 1UL);
    UBRR0H = (uint8_t)(divisor >> 8);
    UBRR0L = (uint8_t)divisor;
    UCSR0B = _BV(TXEN0) | _BV(RXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    /* Release PC2..PC5 from JTAG; the two JTD writes meet the timed sequence. */
    MCUCR |= _BV(JTD);
    MCUCR |= _BV(JTD);
    TCCR0A = _BV(WGM01);
    TCCR0B = _BV(CS01) | _BV(CS00);
    OCR0A = 249;
    TIMSK0 = _BV(OCIE0A);
    memset(&p, 0, sizeof(p));
    p.write = output;
    p.input = input;
    p.poll = poll;
    p.device = device;
    p.nvread = nvread;
    p.nvwrite = nvwrite;
    p.nvsize = 4096;
    memset(&terminal, 0, sizeof(terminal));
    terminal.basic = &basic;
    terminal.platform = &p;
    terminal.source = source;
    terminal.capacity = sizeof(source);
    terminal.steps = UINT32_MAX;
    terminal.seed = 1;
    sei();
    firmware_status = (uint8_t)sb_terminal(&terminal);
    for (;;) {
    }
}
