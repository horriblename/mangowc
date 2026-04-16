#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define GESTURE_MAX_FINGERS 10

// in mm
#define SWIPE_SLOP 0.2

//                 (DETECTING)
//                     |
//         /-----------+--------+--CANCEL--> (CANCELLED)
//         |                    |
//     DRAG_BEGIN              END
//         |                    |
//         v                    v
// (DRAG_IN_PROGRESS)        (ENDED)
//         |                    ^
//         |                    |
//         +--- END/FLING ------/
//         |
//      CANCEL
//         |
//         v
//    (CANCELLED)
enum GesturePhase {
	GESTURE_PHASE_DETECTING,
	GESTURE_PHASE_DRAG_IN_PROGRESS,
	GESTURE_PHASE_CANCELLED,
	GESTURE_PHASE_ENDED,
};

enum GestureEventType {
	GESTURE_EVENT_BEGIN,
	GESTURE_EVENT_CANCEL,
	GESTURE_EVENT_FLING,
	GESTURE_EVENT_END,
	GESTURE_EVENT_NONE,
};

enum GestureKind {
	GESTURE_KIND_NONE,
	GESTURE_KIND_SWIPE,
};

typedef struct GestureEvent {
	enum GesturePhase phase;
	enum GestureEventType event;
	enum GestureKind kind;
} GestureEvent;

typedef struct Finger {
	// -1 means not valid
	int64_t id;
	// output-local coordinate, in mm
	double origin_x, origin_y;
	double current_x, current_y;
} Finger;

typedef struct GestureState {
	int fingers_count;
	Finger fingers[GESTURE_MAX_FINGERS];

	enum GesturePhase phase;
	int32_t start_time;
} GestureState;

typedef struct SwipeDetector {
	enum GestureKind detected_type;
} MultiFingerDetector;

typedef struct GestureDetectors {
	MultiFingerDetector swipe;
	GestureState state;

	enum GestureKind active;
	bool drag_ended;

	double monitor_w_mm, monitor_h_mm;
} GestureDetectors;

typedef struct TouchEvent {
	uint32_t time;
	uint32_t finger_id;
	enum { TOUCH_UP, TOUCH_MOVE, TOUCH_DOWN } type;
	double x, y;
} TouchEvent;

static Finger *find_finger(GestureDetectors *self, int id);
static int find_finger_index(GestureDetectors *self, int id);
static void origin_center(const GestureState *state, int32_t *x, int32_t *y);

static enum GestureEventType
multifinger_detector_update(MultiFingerDetector *self,
							const GestureState *state, const TouchEvent *ev) {
	int32_t origin_x, origin_y;
	const double swipe_slop_2 = SWIPE_SLOP * SWIPE_SLOP;

	origin_center(state, &origin_x, &origin_y);
	switch (state->phase) {
	case GESTURE_PHASE_CANCELLED:
	case GESTURE_PHASE_ENDED:
		return GESTURE_EVENT_NONE;

	case GESTURE_PHASE_DETECTING:
		switch (ev->type) {
		case TOUCH_UP:
			return GESTURE_EVENT_CANCEL;
		case TOUCH_MOVE: {
			double delta_x = origin_x - ev->x;
			double delta_y = origin_y - ev->y;
			double distance = delta_x * delta_x + delta_y * delta_y;

			if (swipe_slop_2 <= distance) {
				self->detected_type = GESTURE_KIND_SWIPE;
				return GESTURE_EVENT_BEGIN;
			} else {
				return GESTURE_EVENT_NONE;
			}
		}
		case TOUCH_DOWN:
			return GESTURE_EVENT_NONE;
		}
		return GESTURE_EVENT_NONE;

	case GESTURE_PHASE_DRAG_IN_PROGRESS:
		switch (self->detected_type) {
		case GESTURE_KIND_NONE:
			return GESTURE_EVENT_CANCEL;
		case GESTURE_KIND_SWIPE:
			switch (ev->type) {
			case TOUCH_UP:
				return state->fingers_count == 0 ? GESTURE_EVENT_END
												 : GESTURE_EVENT_NONE;
			case TOUCH_MOVE:
				return GESTURE_EVENT_NONE;
			case TOUCH_DOWN:
				return GESTURE_EVENT_CANCEL;
			}
		}
		return GESTURE_EVENT_NONE;
	}
	return GESTURE_EVENT_NONE;
}

static void gesture_state_init(GestureState *state) {
	for (int i = 0; i < GESTURE_MAX_FINGERS; i++) {
		state->fingers[i] = (Finger){.id = -1};
	}
}

// Returns null if out-of-space
static Finger *gesture_state_add_finger(GestureState *state, Finger finger) {
	if (state->fingers_count >= GESTURE_MAX_FINGERS) {
		return false;
	}

	for (int i = 0; i < GESTURE_MAX_FINGERS; i++) {
		if (state->fingers[i].id != -1) {
			state->fingers[i] = finger;
			return &state->fingers[i];
		}
	}

	assert(false); // TODO
}

// Returns whether or not the finger was found
static bool gesture_state_remove_finger(GestureDetectors *self,
										GestureState *state, int id) {
	int i = find_finger_index(self, id);
	if (i < 0)
		return false;
	*state->fingers = (Finger){.id = -1};
	state->fingers_count--;
	return true;
}

void gesture_detectors_init(GestureDetectors *d) {
	d->swipe = (MultiFingerDetector){0};
	gesture_state_init(&d->state);
	d->active = GESTURE_KIND_NONE;
	d->drag_ended = false;
	d->monitor_w_mm = 0;
	d->monitor_h_mm = 0;
}

static bool gesture_event_ends_drag(enum GestureEventType event) {
	return event == GESTURE_EVENT_END || event == GESTURE_EVENT_FLING ||
		   event == GESTURE_EVENT_CANCEL;
}

GestureEvent gesture_detectors_touchdown(GestureDetectors *self,
										 uint32_t time_msec, uint32_t id,
										 double x, double y) {
	GestureState *state = &self->state;

	if (state->fingers_count == 0) {
		state->start_time = time_msec;
		state->phase = GESTURE_PHASE_DETECTING;
		self->drag_ended = false;
		self->active = GESTURE_KIND_NONE;
	}

	if (self->drag_ended) {
		return (GestureEvent){state->phase, GESTURE_EVENT_NONE, self->active};
	}

	Finger finger = {
		.id = id,
		.origin_x = x,
		.origin_y = y,
		.current_x = x,
		.current_y = y,
	};
	assert(!find_finger(self, finger.id));
	gesture_state_add_finger(state, finger);

	enum GestureEventType event;
	enum GestureKind kind = GESTURE_KIND_NONE;
	TouchEvent touch = {
		.time = time_msec,
		.finger_id = id,
		.x = x,
		.y = y,
	};
	switch (self->active) {
	case GESTURE_KIND_NONE:
		event = multifinger_detector_update(&self->swipe, state, &touch);
		if (event == GESTURE_EVENT_BEGIN) {
			self->active = GESTURE_KIND_SWIPE;
			state->phase = GESTURE_PHASE_DRAG_IN_PROGRESS;
			kind = GESTURE_KIND_SWIPE;
			break;
		}

	case GESTURE_KIND_SWIPE:
		event = multifinger_detector_update(&self->swipe, state, &touch);
		if (gesture_event_ends_drag(event)) {
			self->drag_ended = true;
			state->phase = GESTURE_PHASE_ENDED;
		}
		break;
	}

	return (GestureEvent){
		.phase = state->phase,
		.event = event,
		.kind = kind,
	};
}

GestureEvent gesture_detectors_touchmove(GestureDetectors *self,
										 uint32_t time_msec, uint32_t id,
										 double x, double y) {
	if (self->drag_ended) {
		return (GestureEvent){self->state.phase, GESTURE_EVENT_NONE,
							  GESTURE_KIND_NONE};
	}

	GestureState *state = &self->state;

	Finger *finger = find_finger(self, id);
	if (finger) {
		finger->current_x = x;
		finger->current_y = y;
	}

	TouchEvent touch = {
		.time = time_msec,
		.finger_id = id,
		.x = x,
		.y = y,
	};
	switch (self->active) {
	case GESTURE_KIND_NONE: {
		TouchEvent ev = {
			.time = time_msec,
			.finger_id = id,
			.type = TOUCH_MOVE,
			.x = x,
			.y = y,
		};
		enum GestureEventType event =
			multifinger_detector_update(&self->swipe, state, &ev);
		if (event == GESTURE_EVENT_BEGIN) {
			assert(self->swipe.detected_type != GESTURE_KIND_NONE);
			self->active = self->swipe.detected_type;
			state->phase = GESTURE_PHASE_DRAG_IN_PROGRESS;
			return (GestureEvent){state->phase, event, self->active};
		}
		return (GestureEvent){state->phase, GESTURE_EVENT_NONE,
							  GESTURE_KIND_NONE};
	}
	case GESTURE_KIND_SWIPE: {
		enum GestureEventType event =
			multifinger_detector_update(&self->swipe, state, &touch);
		if (gesture_event_ends_drag(event)) {
			self->drag_ended = true;
			state->phase = GESTURE_PHASE_ENDED;
		}
		return (GestureEvent){state->phase, event, GESTURE_KIND_SWIPE};
	}
	}
	assert(false); // unreachable
	return (GestureEvent){state->phase, GESTURE_EVENT_NONE, GESTURE_KIND_NONE};
}

GestureEvent gesture_detectors_touchup(GestureDetectors *self,
									   uint32_t time_msec, uint32_t id) {
	if (self->drag_ended) {
		return (GestureEvent){self->state.phase, GESTURE_EVENT_NONE,
							  GESTURE_KIND_NONE};
	}

	GestureState *state = &self->state;

	double x, y;
	{
		Finger *finger = find_finger(self, id);
		if (!finger) {
			return (GestureEvent){state->phase, GESTURE_EVENT_NONE,
								  GESTURE_KIND_NONE};
		}
		x = finger->current_x;
		y = finger->current_y;
	}

	gesture_state_remove_finger(self, state, id);

	TouchEvent touch = {
		.time = time_msec,
		.finger_id = id,
		.type = TOUCH_UP,
		.x = x,
		.y = y,
	};
	switch (self->active) {
	case GESTURE_KIND_NONE: {
		enum GestureEventType event =
			multifinger_detector_update(&self->swipe, state, &touch);
		if (event == GESTURE_EVENT_BEGIN) {
			assert(self->swipe.detected_type != GESTURE_KIND_NONE);
			self->active = self->swipe.detected_type;
			state->phase = GESTURE_PHASE_DRAG_IN_PROGRESS;
			return (GestureEvent){state->phase, event, self->active};
		}
		return (GestureEvent){state->phase, GESTURE_EVENT_NONE,
							  GESTURE_KIND_NONE};
	}
	case GESTURE_KIND_SWIPE: {
		assert(self->active == self->swipe.detected_type);
		enum GestureEventType event =
			multifinger_detector_update(&self->swipe, state, &touch);
		if (gesture_event_ends_drag(event)) {
			self->drag_ended = true;
			state->phase = GESTURE_PHASE_ENDED;
		}
		return (GestureEvent){state->phase, event, self->active};
	}
	}
	assert(false); // unreachable
	return (GestureEvent){state->phase, GESTURE_EVENT_NONE, GESTURE_KIND_NONE};
}

static void origin_center(const GestureState *state, int32_t *x, int32_t *y) {
	if (state->fingers_count == 0) {
		*x = 0;
		*y = 0;
		return;
	}

	double sum_x = 0;
	for (int i = 0; i < state->fingers_count; i++) {
		sum_x += state->fingers[i].origin_x;
	}
	double sum_y = 0;
	for (int i = 0; i < state->fingers_count; i++) {
		sum_y += state->fingers[i].origin_y;
	}
	*x = sum_x / state->fingers_count;
	*y = sum_y / state->fingers_count;
}

static Finger *find_finger(GestureDetectors *self, int id) {
	int i = find_finger_index(self, id);
	if (i < 0)
		return NULL;
	return &self->state.fingers[i];
}

static int find_finger_index(GestureDetectors *self, int id) {
	for (int i = 0; i < GESTURE_MAX_FINGERS; i++) {
		if (self->state.fingers[i].id == id) {
			return i;
		}
	}
	return -1;
}
