#pragma once

#include "gestures.h"
#include <wlr/types/wlr_touch.h>

static void *gestures;

typedef struct Gestures {
} Gestures;

bool gestures_on_touch_down(struct wlr_touch_down_event *event) {
	gesture_detectors_touchdown(event->time_msec, event->touch_id, event->x,
								event->y);
	// TODO
	return false;
}

bool gestures_on_touch_move(struct wlr_touch_motion_event *event) {
	gesture_detectors_touchmove(event->time_msec, event->touch_id, event->x,
								event->y);
	// TODO
	return false;
}

bool gestures_on_touch_up(struct wlr_touch_up_event *event) {
	gesture_detectors_touchup(event->time_msec, event->touch_id);
	// TODO
	return false;
}
