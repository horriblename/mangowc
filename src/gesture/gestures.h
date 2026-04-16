#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define GESTURE_MAX_FINGERS 10

// in mm
#define SWIPE_SLOP 0.2

//    +--> DETECTING
//    |
//    +----------------+--> DRAG_CANCEL
//    |                |
// ---+--> DRAG_BEGIN -+--> DRAG_FLING
//    |                |
//    +----------------+--> END
// TODO: split into phases and a new GestureEventType
// - BEGIN, END, CANCEL and FLING should be GestureEventType
// - GesturePhase should also add a CANCELLED and ENDED, which replaces
// ALREADY_DONE
enum GesturePhase {
	// No gesture detected so far
	GESTURE_PHASE_DETECTING,

	// The gesture is cancelled. When triggered during a drag, the drag is ended
	GESTURE_PHASE_CANCEL,

	// A gesture is detected and enters the drag phase. Some gestures (i.e. tap)
	// do not emit this event and skip straight to GESTURE_PHASE_END
	GESTURE_PHASE_DRAG_BEGIN,

	GESTURE_PHASE_DRAG_IN_PROGRESS, // TODO: I should split phases from gesture
									// event kinds

	// A fling is detected. This ends the drag gesture, if any.
	GESTURE_PHASE_DRAG_FLING,

	// The drag ended, or a gesture without a drag phase is triggered.
	GESTURE_PHASE_END,

	// The gesture already ended/cancelled/flung
	GESTURE_PHASE_ALREADY_DONE,
};

enum GestureKind {
	GESTURE_KIND_NONE,
	GESTURE_KIND_SWIPE,
};

typedef struct GestureEvent {
	enum GesturePhase phase;
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

static Finger *find_finger(int id);
static int find_finger_index(int id);
static void origin_center(const GestureState *state, int32_t *x, int32_t *y);

static enum GesturePhase multifinger_detector_update(MultiFingerDetector *self,
													 const GestureState *state,
													 const TouchEvent *ev) {
	int32_t origin_x, origin_y;
	const double swipe_slop_2 = SWIPE_SLOP * SWIPE_SLOP;

	origin_center(state, &origin_x, &origin_y);
	switch (state->phase) {
	case GESTURE_PHASE_DETECTING:
		switch (ev->type) {
		case TOUCH_UP:
			return GESTURE_PHASE_CANCEL;
		case TOUCH_MOVE: {
			double delta_x = origin_x - ev->x;
			double delta_y = origin_y - ev->y;
			double distance = delta_x * delta_x + delta_y * delta_y;

			if (swipe_slop_2 <= distance) {
				self->detected_type = GESTURE_KIND_SWIPE;
				return GESTURE_PHASE_DRAG_BEGIN;
			} else {
				return GESTURE_PHASE_DETECTING;
			}
		}
		case TOUCH_DOWN:
			return GESTURE_PHASE_DETECTING;
		}
		break;

	case GESTURE_PHASE_ALREADY_DONE:
		return GESTURE_PHASE_ALREADY_DONE;

	case GESTURE_PHASE_DRAG_IN_PROGRESS:
		switch (self->detected_type) {
		case GESTURE_KIND_NONE:
			assert(false); // TODO
		case GESTURE_KIND_SWIPE:
			switch (ev->type) {
			case TOUCH_UP:
				return state->fingers_count == 0
						   ? GESTURE_PHASE_END
						   : GESTURE_PHASE_DRAG_IN_PROGRESS;
			case TOUCH_MOVE:
				return GESTURE_PHASE_DRAG_IN_PROGRESS;
			case TOUCH_DOWN:
				return GESTURE_PHASE_CANCEL;
			}
		}
		return GESTURE_PHASE_DETECTING;

	default:
		assert(false);
		// the rest are "events" rather than phases
		// case GESTURE_PHASE_DRAG_BEGIN:
		// case GESTURE_PHASE_CANCEL:
		// case GESTURE_PHASE_DRAG_FLING:
		// case GESTURE_PHASE_END:
	}
}

GestureDetectors *gesture_detectors;

static void gesture_state_init(GestureState *state) {
	for (int i = 0; i < GESTURE_MAX_FINGERS; i++) {
		state->fingers[i] = (Finger){.id = -1};
	}
}

// Returns null if out-of-space
static Finger *gesture_state_add_finger(GestureState *state, Finger finger) {
	assert(!find_finger(finger.id));
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
static bool gesture_state_remove_finger(GestureState *state, int id) {
	int i = find_finger_index(id);
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

static bool gesture_phase_ends_drag(enum GesturePhase phase) {
	return phase == GESTURE_PHASE_END || phase == GESTURE_PHASE_DRAG_FLING ||
		   phase == GESTURE_PHASE_CANCEL;
}

GestureEvent gesture_detectors_touchdown(uint32_t time_msec, uint32_t id,
										 double x, double y) {
	GestureState *state = &gesture_detectors->state;

	if (state->fingers_count == 0) {
		state->start_time = time_msec;
		gesture_detectors->drag_ended = false;
		gesture_detectors->active = GESTURE_KIND_NONE;
	}

	if (gesture_detectors->drag_ended) {
		return (GestureEvent){GESTURE_PHASE_ALREADY_DONE,
							  gesture_detectors->active};
	}

	Finger finger = {
		.id = id,
		.origin_x = x,
		.origin_y = y,
		.current_x = x,
		.current_y = y,
	};
	gesture_state_add_finger(state, finger);

	enum GesturePhase phase;
	enum GestureKind kind = 0; // TODO
	TouchEvent touch = {
		.time = time_msec,
		.finger_id = id,
		.x = x,
		.y = y,
	};
	switch (gesture_detectors->active) {
	case GESTURE_KIND_NONE:
		phase = multifinger_detector_update(&gesture_detectors->swipe, state,
											&touch);
		if (phase == GESTURE_PHASE_DRAG_BEGIN) {
			gesture_detectors->active = GESTURE_KIND_SWIPE;
			kind = GESTURE_KIND_SWIPE;
			break;
		}

	case GESTURE_KIND_SWIPE:
		phase = multifinger_detector_update(&gesture_detectors->swipe, state,
											&touch);
		if (gesture_phase_ends_drag(phase)) {
			gesture_detectors->drag_ended = true;
		}
		break;
	}

	return (GestureEvent){
		.phase = phase,
		.kind = kind,
	};
}

GestureEvent gesture_detectors_touchmove(uint32_t time_msec, uint32_t id,
										 double x, double y) {
	if (gesture_detectors->drag_ended) {
		return (GestureEvent){GESTURE_PHASE_ALREADY_DONE, GESTURE_KIND_NONE};
	}

	GestureState *state = &gesture_detectors->state;

	Finger *finger = find_finger(id);
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
	switch (gesture_detectors->active) {
	case GESTURE_KIND_NONE: {
		TouchEvent ev = {
			.time = time_msec,
			.finger_id = id,
			.type = TOUCH_MOVE,
			.x = x,
			.y = y,
		};
		enum GesturePhase phase =
			multifinger_detector_update(&gesture_detectors->swipe, state, &ev);
		if (phase == GESTURE_PHASE_DRAG_BEGIN) {
			assert(gesture_detectors->swipe.detected_type != GESTURE_KIND_NONE);
			gesture_detectors->active = gesture_detectors->swipe.detected_type;
			return (GestureEvent){phase, gesture_detectors->active};
		}
	}
	case GESTURE_KIND_SWIPE: {
		enum GesturePhase phase = multifinger_detector_update(
			&gesture_detectors->swipe, state, &touch);
		if (gesture_phase_ends_drag(phase)) {
			gesture_detectors->drag_ended = true;
		}
		return (GestureEvent){phase, GESTURE_KIND_SWIPE};
	}
	}
}

GestureEvent gesture_detectors_touchup(uint32_t time_msec, uint32_t id) {
	if (gesture_detectors->drag_ended) {
		return (GestureEvent){GESTURE_PHASE_ALREADY_DONE, GESTURE_KIND_NONE};
	}

	GestureState *state = &gesture_detectors->state;

	double x, y;
	{
		Finger *finger = find_finger(id);
		if (!finger) {
			return (GestureEvent){GESTURE_PHASE_ALREADY_DONE,
								  GESTURE_KIND_NONE};
		}
		x = finger->current_x;
		y = finger->current_y;
	}

	gesture_state_remove_finger(state, id);

	TouchEvent touch = {
		.time = time_msec,
		.finger_id = id,
		.type = TOUCH_UP,
		.x = x,
		.y = y,
	};
	switch (gesture_detectors->active) {
	case GESTURE_KIND_NONE: {
		enum GesturePhase phase = multifinger_detector_update(
			&gesture_detectors->swipe, state, &touch);
		if (phase == GESTURE_PHASE_DRAG_BEGIN) {
			assert(gesture_detectors->swipe.detected_type != GESTURE_KIND_NONE);
			gesture_detectors->active = gesture_detectors->swipe.detected_type;
			return (GestureEvent){phase, gesture_detectors->active};
		}
	}
	case GESTURE_KIND_SWIPE: {
		assert(gesture_detectors->active ==
			   gesture_detectors->swipe.detected_type);
		enum GesturePhase phase = multifinger_detector_update(
			&gesture_detectors->swipe, state, &touch);
		if (gesture_phase_ends_drag(phase)) {
			gesture_detectors->drag_ended = true;
		}
		return (GestureEvent){phase, gesture_detectors->active};
	}
	}
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

static Finger *find_finger(int id) {
	int i = find_finger_index(id);
	if (i < 0)
		return NULL;
	return &gesture_detectors->state.fingers[i];
}

static int find_finger_index(int id) {
	for (int i = 0; i < GESTURE_MAX_FINGERS; i++) {
		if (gesture_detectors->state.fingers[i].id == id) {
			return i;
		}
	}
	return -1;
}
