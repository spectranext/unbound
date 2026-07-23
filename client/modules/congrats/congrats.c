#include <spectrum.h>
#include "modules.h"
#include "state.h"
#include "text_ui.h"
#include "zxgui.h"
#include "soundfx.h"
#include "client.h"
#include "client_graphics.h"

static char state = '.';
static uint8_t stat_line = 0;
static uint8_t stat_line_color = 0;
static char line[65];

extern void render_blink_even(uint8_t *addr) __z88dk_fastcall __naked;
extern void render_blink_odd(uint8_t *addr) __z88dk_fastcall __naked;
extern void clear_blink_even(uint8_t *addr) __z88dk_fastcall __naked;
extern void clear_blink_odd(uint8_t *addr) __z88dk_fastcall __naked;

static void wait(uint8_t time) __z88dk_fastcall
{
    for (uint8_t i = 0; i < time; i++)
    {
#ifndef __JETBRAINS_IDE__
#asm
        halt
#endasm
#endif
    }
}

static void write_line(uint8_t v, const char* text, uint8_t color, uint8_t pre_wait, uint8_t wait_time)
{
    uint8_t l = strlen(text);
    screen_color = INK_YELLOW | BRIGHT | PAPER_BLACK;
    zxgui_screen_clear(0, v, 32, 1);

    uint8_t* addr = zx_cxy2saddr(0, v);
    for (uint8_t i = 0; i < pre_wait; i++)
    {
        render_blink_odd(addr);
        wait(10);
    }

    screen_color = color;
    zxgui_screen_clear(0, v, 32, 1);

    uint8_t i;
    text_ui_color(color);
    uint8_t location = 0;
    for (i = 1; i < l; i++)
    {
        text_ui_write_at(location, v, text, (i & 0x01) + 1);
        uint8_t k = i + 1;
        addr = zx_cxy2saddr(k / 2, v);
        if (k & 0x01)
        {
            render_blink_even(addr);
            wait(1);
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

    addr = zx_cxy2saddr(i / 2, v);

    if (wait_time)
    {
        for (uint8_t i2 = 0; i2 < wait_time; i2++)
        {
            if (i & 0x01)
            {
                render_blink_even(addr);
            }
            else
            {
                render_blink_odd(addr);
            }
            wait(10);
        }
    }

    if (i & 0x01)
    {
        clear_blink_even(addr);
    }
    else
    {
        clear_blink_odd(addr);
    }
}

void module_loop()
{
    switch (state)
    {
        case 'c':
        {
            zxgui_clear();
            client_action("done");
            state = '.';
            break;
        }
        case 'l':
        {
            write_line(23, line, INK_GREEN | PAPER_BLACK | BRIGHT, 4, 8);
            screen_color = INK_BLACK | PAPER_BLACK;
            zxgui_screen_clear(0, 23, 32, 0);
            client_action("done");
            state = '.';
            break;
        }
        case 's':
        {
            write_line(stat_line++, line, stat_line_color, 0, 0);
            client_action("done");
            state = '.';
            break;
        }
        case 'e':
        {
            state = '.';
            client_map_b.screen_dirty = 1;
            rendering_blocked = 0;
            module_loop_active = 0xFF;
            break;
        }
    }
}

void module_action(ProtoObject* proto_object) __z88dk_fastcall
{
    state = (char)get_uint8_property(proto_object, 's', 0);

    rendering_blocked = 1;

    switch (state)
    {
        // push a line
        case 'l':
        {
            get_str_property(proto_object, 'l', line, sizeof(line));
            break;
        }
        // push a stat
        case 's':
        {
            // offset
            stat_line += get_uint8_property(proto_object, 'o', 0);;
            get_str_property(proto_object, 'l', line, sizeof(line));
            stat_line_color = get_uint8_property(proto_object, 'c', 0);
            break;
        }
    }

    module_loop_active = 0;
}

void module_interrupt() {}