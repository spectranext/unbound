#include "system.h"
#include "zxgui.h"
#include "scenes.h"

extern char alert_text_data[64];

#include "alert.inc.h"
#include "client.h"
#include "modules.h"

static void alert_connect()
{
    restart_to_main();
}

static void alert_restart()
{
    restart_to_main();
}

void switch_alert(const char* progress_message)
{
    strcpy(alert_text_data, progress_message);
    alert_text.base.flags |= GUI_FLAG_DIRTY;
    connect_btn.base.flags |= GUI_FLAG_DIRTY;
    restart_btn.base.flags |= GUI_FLAG_DIRTY;
    zxgui_clear();
    current_scene_module = MODULE_NONE;
    zxgui_scene_set(&scene);
}
