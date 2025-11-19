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

typedef struct {
    float x;
    float y;
    float pressure;
    int   down;      // 0/1
    int   device_id; // optional
} RawPen;

RawPen *mac_pen_get(void);  // returns pointer to pen if valid, NULL if no pen

#ifdef __cplusplus
}
#endif
