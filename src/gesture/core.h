#pragma once

#include "gestures.h"
#include <wlr/types/wlr_touch.h>

static void *gestures;

typedef struct Gestures {
} Gestures;

bool gestures_on_touch_down(GestureDetectors *detector,
							struct wlr_touch_down_event *event, double mon_w_mm,
							double mon_h_mm) {
	double x = event->x * mon_w_mm;
	double y = event->y * mon_h_mm;
	detector->monitor_w_mm = mon_w_mm;
	detector->monitor_h_mm = mon_h_mm;
	gesture_detectors_touchdown(event->time_msec, event->touch_id, x, y);
	// TODO
	return false;
}

bool gestures_on_touch_move(GestureDetectors *detector,
							struct wlr_touch_motion_event *event) {
	double x = event->x * detector->monitor_w_mm;
	double y = event->y * detector->monitor_h_mm;
	gesture_detectors_touchmove(event->time_msec, event->touch_id, x, y);
	// TODO
	return false;
}

bool gestures_on_touch_up(GestureDetectors *detector,
						  struct wlr_touch_up_event *event) {
	gesture_detectors_touchup(event->time_msec, event->touch_id);
	// TODO
	return false;
}
