#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 传感器快照（main 任务写，httpd 任务读；portMUX 保护） */
typedef struct {
    float   temp_c;
    float   rh;
    bool    motion;   /* SR501 高电平 = 有人活动 */
    bool    aht_ok;   /* 温湿度至少成功读过一次 */
    int64_t t_ms;     /* 最近一次温湿度成功时刻（ms） */
} env_snapshot_t;

void env_update(float temp_c, float rh);
void env_set_motion(bool motion);
bool env_get(env_snapshot_t *out);
