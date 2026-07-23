#include <spectrum.h>
#include "modules.h"
#include "notifications.h"
#include "state.h"
#include "text_ui.h"
#include "zxgui.h"
#include "soundfx.h"
#include "client.h"
#include "key_controls.h"

static uint8_t state = 0;

extern void render_blink_even(uint8_t *addr) __z88dk_fastcall __naked;
extern void render_blink_odd(uint8_t *addr) __z88dk_fastcall __naked;

uint8_t check_exit()
{
    uint8_t row = poll_key_row(ROW_SPACE_SYM_MNB);
    if ((row & KEY_BIT_SPACE) == 0)
    {
        zxgui_clear();
        client_action("skip");
        state = 0xFF;
        return 1;
    }

    return 0;
}

static uint8_t wait(uint8_t time) __z88dk_fastcall
{
    for (uint8_t i = 0; i < time; i++)
    {
#ifndef __JETBRAINS_IDE__
#asm
        halt
#endasm
#endif
        if (check_exit()) return 1;
    }

    return 0;
}

static uint8_t write_line(const char* text, uint8_t color)
{
    const char* orig = text;
    uint8_t l = strlen(text);
    screen_color = INK_YELLOW | BRIGHT | PAPER_BLACK;
    zxgui_screen_clear(0, 23, 32, 1);

    if (check_exit()) return 1;

    uint8_t* addr = zx_cxy2saddr(0, 23);
    for (uint8_t i = 0; i < 4; i++)
    {
        render_blink_odd(addr);
        if (wait(10)) return 1;
    }

    screen_color = color;
    zxgui_screen_clear(0, 23, 32, 1);

    uint8_t i;
    text_ui_color(color);
    uint8_t location = 0;
    for (i = 1; i < l; i++)
    {
        if (check_exit()) return 1;
        text_ui_write_at(location, 23, text, (i & 0x01) + 1);
        uint8_t k = i + 1;
        addr = zx_cxy2saddr(k / 2, 23);
        if (k & 0x01)
        {
            render_blink_even(addr);
            if (check_exit()) return 1;
            if (wait(1)) return 1;
        }
        else
        {
            render_blink_odd(addr);
            if (i & 0x2)
            {
                soundfx(FX_BEEP);
            }
            location++;
            text += 2;
        }
    }

    addr = zx_cxy2saddr(i / 2, 23);

    for (uint8_t i2 = 0; i2 < 8; i2++)
    {
        if (i & 0x01)
        {
            render_blink_even(addr);
        }
        else
        {
            render_blink_odd(addr);
        }
        if (wait(10)) return 1;
    }

    screen_color = INK_BLACK | PAPER_BLACK;
    zxgui_screen_clear(0, 23, 32, 1);
    if (check_exit()) return 1;
    return 0;
}

#define WRITE_LINE(x, y) if (write_line(x, y)) return;

void module_loop()
{
    if (check_exit()) return;

    switch (state)
    {
        case 0:
        {
            text_color = INK_WHITE | PAPER_BLACK;
            text_ui_puts_at(0, 0, "Press SPACE to skip intro");

            WRITE_LINE("TIME: 10 OCTOBER 2048 ", INK_RED | PAPER_BLACK | BRIGHT);
            WRITE_LINE("LOCATION: 128 AU from Earth ", INK_RED | PAPER_BLACK | BRIGHT);
            client_action("done");
            state = 0xFF;
            break;
        }
        case 1:
        {
            WRITE_LINE("CENTRAL: UNBOUND, this is Station Control. Over.", INK_YELLOW | PAPER_BLACK | BRIGHT);
            WRITE_LINE("UNBOUND: Station Control, this is UNBOUND. Go ahead. Over.", INK_GREEN | PAPER_BLACK | BRIGHT);
            WRITE_LINE("CENTRAL: UNBOUND, cleared for departure on vector 270.", INK_YELLOW | PAPER_BLACK | BRIGHT);
            WRITE_LINE("... report every 10 minutes. Safe travels. Over.", INK_YELLOW | PAPER_BLACK | BRIGHT);
            WRITE_LINE("UNBOUND: Central, understood. Reporting every 10. Out.", INK_GREEN | PAPER_BLACK | BRIGHT);
            client_action("done");
            state = 0xFF;
            break;
        }
        case 2:
        {
            state = 0xFF;
            zxgui_clear();
            module_loop_active = 0xFF;
            break;
        }
    }
}

void module_action(ProtoObject* proto_object) __z88dk_fastcall
{
    state = get_uint8_property(proto_object, 's', 0);
    module_loop_active = 0;
}

void module_interrupt() {}