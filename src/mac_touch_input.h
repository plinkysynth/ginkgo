#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id;         // stable per finger
    float x, y;     // normalized 0..1 in view coordinates
    float pressure;
} RawTouch;

int mac_touch_get(RawTouch *out, int max_count, float fbw, float fbh);  // returns number of touches
void mac_touch_init(void *cocoa_window);          // pass NSWindow* from GLFW

#ifdef __cplusplus
}
#endif
