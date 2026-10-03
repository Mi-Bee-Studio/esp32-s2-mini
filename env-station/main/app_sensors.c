/* app_sensors — 传感器快照（main 任务写 / Web 任务读，portMUX 保护） */
#include "app_sensors.h"

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static env_snapshot_t s_snap = { .temp_c = 0.0f, .rh = 0.0f, .motion = false, .aht_ok = false };

void env_update(float temp_c, float rh)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.temp_c = temp_c;
    s_snap.rh = rh;
    s_snap.aht_ok = true;
    s_snap.t_ms = esp_timer_get_time() / 1000;
    portEXIT_CRITICAL(&s_mux);
}

void env_set_motion(bool motion)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.motion = motion;
    portEXIT_CRITICAL(&s_mux);
}

bool env_get(env_snapshot_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_snap;
    portEXIT_CRITICAL(&s_mux);
    return out->aht_ok;
}
