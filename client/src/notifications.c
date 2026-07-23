#include "notifications.h"
#include <stdint.h>
#include <string.h>
#include <intrinsic.h>
#include "client_graphics.h"
#include "arch/zx/spectrum.h"
#include "zxgui.h"
#include "state.h"
#include "text_ui.h"
#include "soundfx.h"

enum notification_state_t notification_state = NOTIFICATION_STATE_NONE;
static uint8_t notifications_timer = 0;
static uint8_t target_color = 0;
uint8_t border_timer = 0;
uint8_t last_notification_progress = 0xFF;

void show_notification(const char* text, uint8_t len, uint8_t color)
{
    if (rendering_blocked)
        return;

    if (notification_state == NOTIFICATION_STATE_BLOCKED)
    {
        return;
    }

    if (*text == '@')
    {
        text++;
        len--;
    }
    else
    {
        intrinsic_di();
        if ((color & 0x07) == INK_RED)
        {
#ifndef __CLION_IDE__
#asm
            ld a, 2
            ld (_screen_border),a
            out (254),a
            ld a, 5
            ld (_border_timer),a
#endasm
#endif
            soundfx(FX_DAMAGE);
        }
        else
        {
            soundfx(FX_PICK);
        }
        intrinsic_ei();
    }

    target_color = color;
    zxgui_screen_color(INK_WHITE | BRIGHT | PAPER_WHITE);
    zxgui_screen_clear(0, 23, 32, 1);
    text_color = screen_color;
    if (len > 64)
        len = 64;
    text_ui_write_at(0, 23, text, len);
    notification_state = NOTIFICATION_STATE_FLASH;
    notifications_timer = 2;
}

void clear_notification_progress()
{
    if (rendering_blocked)
        return;

    if (notification_state == NOTIFICATION_STATE_BLOCKED)
    {
        return;
    }

    zxgui_screen_color(INK_BLACK | PAPER_BLACK);
    zxgui_screen_clear(20, 0, 12, 1);
    last_notification_progress = 0xFF;

    render_my_stats();
}

void show_notification_progress(uint8_t progress)
{
    if (rendering_blocked)
        return;

    if (last_notification_progress == progress)
    {
        return;
    }

    zxgui_screen_color(INK_CYAN | PAPER_BLACK | BRIGHT);

    zxgui_screen_put(20, 0, progress ? GUI_PROGRESS_FILLED_LEFT : GUI_PROGRESS_LEFT);

    static uint8_t i;
    for (i = 1; i < 11; ++i)
    {
        zxgui_screen_put(20 + i, 0, i < progress ? GUI_PROGRESS_FILLED_MIDDLE : GUI_PROGRESS_MIDDLE);
    }

    zxgui_screen_put(31, 0, GUI_PROGRESS_RIGHT);

    last_notification_progress = progress;
}

void update_notifications()
{
#ifndef __CLION_IDE__
#asm
        ld hl, _border_timer
        ld a, (hl)
        or a
        jr z, border_timer_done
        dec (hl)
        jr nz, border_timer_done
        xor a
        ld (_screen_border),a
        out (254),a
border_timer_done:
#endasm
#endif

    if (rendering_blocked)
        return;

    if (notification_state == NOTIFICATION_STATE_BLOCKED)
    {
        return;
    }

    switch (notification_state)
    {
        case NOTIFICATION_STATE_FLASH:
        {
            if (--notifications_timer == 0)
            {
                zxgui_screen_color(target_color);
                zxgui_screen_recolor(0, 23, 32, 1);

                notification_state = NOTIFICATION_STATE_SHOWING;
                notifications_timer = 200;
            }
            break;
        }
        case NOTIFICATION_STATE_SHOWING:
        {
            if (--notifications_timer == 0)
            {
                zxgui_screen_color(INK_BLACK | PAPER_BLACK);
                zxgui_screen_recolor(0, 23, 32, 1);
                notification_state = NOTIFICATION_STATE_NONE;
                // if we're in construction mode, refresh that
                construction_mode_dirty = 1;
            }
            break;
        }
        default:
        {
            break;
        }
    }
}
