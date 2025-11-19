// touch_input.mm
#import <Cocoa/Cocoa.h>
#import "mac_touch_input.h"

static NSMutableDictionary<id, NSValue *> *g_touches = nil;
static int g_next_id = 1;

static RawPen g_pen;
static BOOL g_pen_valid = NO;

@interface TouchView : NSView
@end

@implementation TouchView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        if (@available(macOS 10.12.2, *)) {
            self.allowedTouchTypes = NSTouchTypeMaskIndirect; // trackpad
        }
        self.wantsRestingTouches = YES;
        self.window.acceptsMouseMovedEvents = YES;

    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

#pragma mark - Trackpad (existing)


- (void)handleTouchesEvent:(NSEvent *)event {
    if (!g_touches)
        g_touches = [[NSMutableDictionary alloc] init];

    NSSet<NSTouch *> *touches = [event touchesMatchingPhase:NSTouchPhaseAny inView:self];

    for (NSTouch *t in touches) {
        id key = t.identity ?: t;

        // remove ended or cancelled touches
        if (t.phase == NSTouchPhaseEnded || t.phase == NSTouchPhaseCancelled) {
            [g_touches removeObjectForKey:key];
            continue;
        }

        RawTouch rt;
        NSValue *existing = g_touches[key];
        if (existing)
            [existing getValue:&rt];
        else
            rt.id = g_next_id++;

        NSPoint p = t.normalizedPosition;
        rt.x = (float)p.x;
        rt.y = (float)p.y;

        g_touches[key] = [NSValue valueWithBytes:&rt objCType:@encode(RawTouch)];
    }

    if (g_touches.count == 0)
        g_next_id = 1;
}

- (void)touchesBeganWithEvent:(NSEvent *)event {
    [self handleTouchesEvent:event];
}
- (void)touchesMovedWithEvent:(NSEvent *)event {
    [self handleTouchesEvent:event];
}
- (void)touchesEndedWithEvent:(NSEvent *)event {
    [self handleTouchesEvent:event];
}
- (void)touchesCancelledWithEvent:(NSEvent *)event {
    [self handleTouchesEvent:event];
}

#pragma mark - Stylus (tablet)

- (BOOL)shouldBeTreatedAsInkEvent:(NSEvent *)event {
    return NO;
}

- (void)mouseDown:(NSEvent *)event   { [self handleMouseTabletEvent:event down:1]; }
- (void)mouseDragged:(NSEvent *)event{ [self handleMouseTabletEvent:event down:1]; }
- (void)mouseUp:(NSEvent *)event     { [self handleMouseTabletEvent:event down:0]; }

- (void)handleMouseTabletEvent:(NSEvent *)event down:(int)down {
    if (event.subtype != NSEventSubtypeTabletPoint)
        return;

    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSRect  b = self.bounds;

    g_pen.x = (float)((p.x - b.origin.x) / b.size.width);
    g_pen.y = 1.f - (float)((p.y - b.origin.y) / b.size.height);
    g_pen.pressure = (float)event.pressure;
    g_pen.down = down;
    g_pen_valid = YES;
}

- (void)tabletPoint:(NSEvent *)event {
    // ignore if it's not actually a pen device (optional, but nice)
    if (event.pointingDeviceType != NSPointingDeviceTypePen)
        return;

    // normalised window coords: convert to view and then 0–1
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSRect  b = self.bounds;
    if (b.size.width <= 0 || b.size.height <= 0)
        return;

    g_pen.x = (float)((p.x - b.origin.x) / b.size.width);
    g_pen.y = 1.f - (float)((p.y - b.origin.y) / b.size.height);
    g_pen.pressure = (float)event.pressure;       // 0..1 for Wacom
    g_pen.device_id = (int)event.deviceID;
    g_pen.down = (g_pen.pressure > 0.0f);         // simple heuristic

    g_pen_valid = YES;
}

- (void)tabletProximity:(NSEvent *)event {
    // leaving proximity → clear pen
    if (!event.isEnteringProximity) {
        g_pen_valid = NO;
        g_pen.pressure = 0.0f;
        g_pen.down = 0;
    }
}

@end

void mac_touch_init(void *cocoa_window) {
    NSWindow *win = (__bridge NSWindow *)cocoa_window;
    if (!win)
        return;

    NSView *content = win.contentView;
    if (!content)
        return;

    TouchView *tv = [[TouchView alloc] initWithFrame:content.bounds];
    tv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;


    [content addSubview:tv positioned:NSWindowAbove relativeTo:nil];
    [win makeFirstResponder:tv];
    win.acceptsMouseMovedEvents = YES; // for tabletPoint reliability

}

int mac_touch_get(RawTouch *out, int max_count, float fbw, float fbh) {
    if (!out || max_count <= 0 || !g_touches)
        return 0;

    int i = 0;
    for (NSValue *val in g_touches.objectEnumerator) {
        if (i >= max_count)
            break;
        RawTouch rt;
        [val getValue:&rt];
        rt.x *= fbw;
        rt.y = (1.f - rt.y) * fbh;
        out[i++] = rt;
    }
    return i;
}

RawPen *mac_pen_get(void) {
    if (!g_pen_valid)
        return 0;
    return &g_pen;
}
