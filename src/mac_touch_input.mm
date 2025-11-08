// touch_input.mm
#import <Cocoa/Cocoa.h>
#import "mac_touch_input.h"

static NSMutableDictionary<id, NSValue *> *g_touches = nil;
static int g_next_id = 1;

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
    }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)handleTouchesEvent:(NSEvent *)event {
    if (!g_touches) g_touches = [[NSMutableDictionary alloc] init];

    NSSet<NSTouch *> *touches =
        [event touchesMatchingPhase:NSTouchPhaseAny inView:self];

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

    if (g_touches.count == 0) g_next_id = 1;
}

- (void)touchesBeganWithEvent:(NSEvent *)event     { [self handleTouchesEvent:event]; }
- (void)touchesMovedWithEvent:(NSEvent *)event     { [self handleTouchesEvent:event]; }
- (void)touchesEndedWithEvent:(NSEvent *)event     { [self handleTouchesEvent:event]; }
- (void)touchesCancelledWithEvent:(NSEvent *)event { [self handleTouchesEvent:event]; }

@end

void mac_touch_init(void *cocoa_window) {
    NSWindow *win = (__bridge NSWindow *)cocoa_window;
    if (!win) return;

    NSView *content = win.contentView;
    if (!content) return;

    TouchView *tv = [[TouchView alloc] initWithFrame:content.bounds];
    tv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    [content addSubview:tv positioned:NSWindowAbove relativeTo:nil];
    [win makeFirstResponder:tv];
}

int mac_touch_get(RawTouch *out, int max_count, float fbw, float fbh) {
    if (!out || max_count <= 0 || !g_touches) return 0;

    int i = 0;
    for (NSValue *val in g_touches.objectEnumerator) {
        if (i >= max_count) break;
        RawTouch rt;
        [val getValue:&rt];
        rt.x *= fbw;
        rt.y = (1.f - rt.y) * fbh;
        out[i++] = rt;
    }
    return i;
}
