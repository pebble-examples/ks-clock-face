#include <pebble.h>

#define COLORS PBL_IF_COLOR_ELSE(true, false)
#define ANTIALIASING true
#define HAND_MARGIN 10
#define ANIMATION_DURATION 500
#define ANIMATION_DELAY 600

typedef struct {
  uint8_t hours;
  uint8_t minutes;
} Time;

static Window *s_main_window;
static Layer *s_canvas_layer;

static GPoint s_center;
static Time s_last_time, s_anim_time;
static uint8_t s_radius = 0, s_anim_hours_60 = 0, s_color_channels[3];
static uint8_t s_radius_final;
static bool s_animating = false;

/*************************** AnimationImplementation **************************/

static void prv_animation_started(Animation *anim, void *context) {
  s_animating = true;
}

static void prv_animation_stopped(Animation *anim, bool stopped, void *context) {
  s_animating = false;
}

static void prv_animate(int duration, int delay, AnimationImplementation *implementation, bool handlers) {
  Animation *anim = animation_create();
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, implementation);
  if (handlers) {
    animation_set_handlers(anim, (AnimationHandlers) {
      .started = prv_animation_started,
      .stopped = prv_animation_stopped
    }, NULL);
  }
  animation_schedule(anim);
}

/************************************ UI **************************************/

static void prv_tick_handler(struct tm *tick_time, TimeUnits changed) {
  // Store time
  s_last_time.hours = tick_time->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = tick_time->tm_min;

  for (int i = 0; i < 3; i++) {
    s_color_channels[i] = rand() % 256;
  }

  // Redraw
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static int prv_hours_to_minutes(int hours_out_of_12) {
  return hours_out_of_12 * 60 / 12;
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  GRect full_bounds = layer_get_bounds(layer);
  GRect bounds = layer_get_unobstructed_bounds(layer);
  s_center = grect_center_point(&bounds);

  // Color background?
  if (COLORS) {
    graphics_context_set_fill_color(ctx, GColorFromRGB(s_color_channels[0], s_color_channels[1], s_color_channels[2]));
  } else {
    graphics_context_set_fill_color(ctx, GColorDarkGray);
  }
  graphics_fill_rect(ctx, full_bounds, 0, GCornerNone);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 4);

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  // White clockface
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, s_center, s_radius);

  // Draw outline
  graphics_draw_circle(ctx, s_center, s_radius);

  // Don't use current time while animating
  Time mode_time = (s_animating) ? s_anim_time : s_last_time;

  // Adjust for minutes through the hour
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle;
  if (s_animating) {
    // Hours out of 60 for smoothness
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 60;
  } else {
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;
  }
  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.y,
  };
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.y,
  };

  // Draw hands with positive length only
  if (s_radius > 2 * HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, hour_hand);
  }
  if (s_radius > HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, minute_hand);
  }
}

static int prv_anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(dist_normalized * max / ANIMATION_NORMALIZED_MAX);
}

static void prv_radius_update(Animation *anim, AnimationProgress dist_normalized) {
  s_radius = prv_anim_percentage(dist_normalized, s_radius_final);

  layer_mark_dirty(s_canvas_layer);
}

static void prv_hands_update(Animation *anim, AnimationProgress dist_normalized) {
  s_anim_time.hours = prv_anim_percentage(dist_normalized, prv_hours_to_minutes(s_last_time.hours));
  s_anim_time.minutes = prv_anim_percentage(dist_normalized, s_last_time.minutes);

  layer_mark_dirty(s_canvas_layer);
}

static void prv_start_animation() {
  // Prepare animations
  static AnimationImplementation s_radius_impl = {
    .update = prv_radius_update
  };
  prv_animate(ANIMATION_DURATION, ANIMATION_DELAY, &s_radius_impl, false);

  static AnimationImplementation s_hands_impl = {
    .update = prv_hands_update
  };
  prv_animate(2 * ANIMATION_DURATION, ANIMATION_DELAY, &s_hands_impl, true);
}

static void prv_create_canvas() {
  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  s_radius_final = (bounds.size.w - 30) / 2;

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

/*********************************** App **************************************/

// Event fires once, before the obstruction appears or disappears
static void prv_unobstructed_will_change(GRect final_unobstructed_screen_area, void *context) {
  if(s_animating) {
    return;
  }
  // Reset the clock animation
  s_radius = 0;
  s_anim_hours_60 = 0;
}

// Event fires once, after obstruction appears or disappears
static void prv_unobstructed_did_change(void *context) {
  if(s_animating) {
    return;
  }
  // Play the clock animation
  prv_start_animation();
}

static void prv_window_load(Window *window) {
  prv_create_canvas();

  prv_start_animation();

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);

  // Subscribe to the unobstructed area events
  UnobstructedAreaHandlers handlers = {
    .will_change = prv_unobstructed_will_change,
    .did_change = prv_unobstructed_did_change
  };
  unobstructed_area_service_subscribe(handlers, NULL);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

static void prv_init() {
  srand(time(NULL));

  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  prv_tick_handler(time_now, MINUTE_UNIT);

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_main_window, true);
}

static void prv_deinit() {
  window_destroy(s_main_window);
}

int main() {
  prv_init();
  app_event_loop();
  prv_deinit();
}
