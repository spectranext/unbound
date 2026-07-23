#include "printf.h"

#include <stdint.h>

extern void spectranext_fputc_cons(char c);

#define print_char spectranext_fputc_cons

void print(const char* s)
{
    if (s == 0)
    {
        s = "(null)";
    }

    while (*s)
    {
        print_char(*s++);
    }
}

void printn(uint16_t value)
{
    static char buf[6];
    uint8_t i = 0;

    if (value == 0)
    {
        print_char('0');
        print_char(' ');
        return;
    }

    while (value && i < sizeof(buf))
    {
        uint16_t next = value / 10;
        buf[i++] = '0' + (value - next * 10);
        value = next;
    }

    while (i)
    {
        print_char(buf[--i]);
    }

    print_char(' ');
}
