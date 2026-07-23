#include <spectrum.h>
#include "modules.h"
#include "state.h"
#include "zxgui.h"
#include "client.h"
#include "client_graphics.h"
#include <intrinsic.h>
#include "messages.h"
#include "key_controls.h"
#include "proto.h"

#define STATE_NONE '.'
#define STATE_CLEAR 'c'
#define STATE_SCREEN 's'
#define STATE_EXIT 'e'
#define ROWS_COUNT 8

#define QUEUE_SIZE 16

static char state = '.';
static uint8_t old_border;

static uint16_t rows[ROWS_COUNT] = {
    ROW_SHIFT_ZXCV,
    ROW_ASDFG,
    ROW_QWERT,
    ROW_12345,
    ROW_09876,
    ROW_POIUY,
    ROW_ENTER_LKJH,
    ROW_SPACE_SYM_MNB
};

static uint8_t values[ROWS_COUNT] = {
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111
};

struct key_push_item_t {
    uint8_t row;
    uint8_t val;
};

static struct key_push_item_t queue[QUEUE_SIZE];
static uint8_t queue_size = 0;

void set_border() __naked
{
#asm
    extern _screen_border
    ld a, (_screen_border)
    out (254),a
    ret
#endasm
}

void notify_keyboard(struct key_push_item_t* item)
{
    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_ACTION, NULL);
    declare_str_property_on_stack(version, 'm', "keyboard", &req_id);
    declare_variable_property_on_stack(p, 'p', item, sizeof(struct key_push_item_t), &version);
    declare_object_on_stack(request, 128, &p);

    proto_send_one_nf(request);
}

void module_loop()
{
    switch (state)
    {
        case STATE_CLEAR:
        {
            zxgui_clear();
            set_border();
            client_action("done");
            state = STATE_NONE;
            break;
        }
        case STATE_SCREEN:
        {
            module_interrupt_active = 0;

            intrinsic_di();
            for (uint8_t i = 0; i < queue_size; i++)
            {
                notify_keyboard(&queue[i]);
            }
            queue_size = 0;
            intrinsic_ei();

            break;
        }
        case STATE_EXIT:
        {
            state = STATE_NONE;
            client_map_b.any_cached_chunks_dirty = 1;
            client_map_b.screen_dirty = 1;
            rendering_blocked = 0;
            module_loop_active = 0xFF;
            screen_border = old_border;
            set_border();
            break;
        }
    }
}

void module_action(ProtoObject* proto_object) __z88dk_fastcall
{
    state = (char)get_uint8_property(proto_object, 's', 0);

    switch (state)
    {
        case STATE_CLEAR:
        {
            old_border = screen_border;
            screen_border = get_uint8_property(proto_object, 'b', 0);
        }
    }

    rendering_blocked = 1;
    module_loop_active = 0;
}

void module_interrupt()
{
    for (uint8_t row = 0; row < ROWS_COUNT; row++)
    {
        uint8_t a = poll_key_row(rows[row]) & 0b11111;
        uint8_t old = values[row] & 0b11111;
        if (a == old)
            continue;
        values[row] = a;

        if (queue_size >= QUEUE_SIZE)
            return;

        struct key_push_item_t* q = &queue[queue_size];
        q->row = rows[row] >> 8;
        q->val = a;
        queue_size++;
    }
}