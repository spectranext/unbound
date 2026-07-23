#include <string.h>
#include <spectrum.h>
#include "scenes.h"
#include "alert.inc.h"

static void alert_connect()
{
    switch_main();
}

void switch_alert(const char* progress_message)
{
    strcpy(alert_text_data, progress_message);
    zxgui_clear();
    zxgui_scene_set(&scene);
}