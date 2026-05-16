// license:GPLv3+

#pragma once

// Needed by ImPlot when using ImGUI
#define IMGUI_DEFINE_MATH_OPERATORS

#define COMPRESS_MESHES // uses miniz for compressing the meshes


//#define DEBUG_NUDGE // debug new nudge code

//#define DEBUG_NO_SOUND
//#define DEBUG_REFCOUNT_TRIGGER

#define EDITOR_BG_WIDTH    1000
#define EDITOR_BG_HEIGHT   750

#define MAIN_WINDOW_WIDTH  1280
#define MAIN_WINDOW_HEIGHT (720-50)

#define BASEDEPTHBIAS 5e-5f

#define THREADS_PAUSE 1000 // msecs/time to wait for threads to finish up

#include "physics/physconst.h"

//

#define MAX_REELS          32

#define LIGHTSEQGRIDSCALE  20
#define	LIGHTSEQGRIDWIDTH  (EDITOR_BG_WIDTH/LIGHTSEQGRIDSCALE)
#define	LIGHTSEQGRIDHEIGHT ((2*EDITOR_BG_WIDTH)/LIGHTSEQGRIDSCALE)

#define LIGHTSEQQUEUESIZE  100

#define MAX_LIGHT_SOURCES  2
#define MAX_BALL_LIGHT_SOURCES  8

//

#define ADAPT_VSYNC_FACTOR 0.95 // safety factor where vsync is turned off (f.e. drops below 60fps * 0.95 = 57fps)

#define ACCURATETIMERS          // if undefd, timers will only be triggered as often as frames are rendered (e.g. they can fall behind)
#define MAX_TIMERS_MSEC_OVERALL 5 // amount of msecs that all timers combined can take per frame (e.g. they can fall behind, if set to < somelargevalue)

//#define DEBUGPHYSICS          // enables detailed physics/collision handling output for the 'F11' stats/debug texts

#if defined(_DEBUG)
#define DEBUG_BALL_SPIN         // enables dots glued to balls if in 'F11' mode
#endif

//

#define LAST_OPENED_TABLE_COUNT 8

#define MAX_CUSTOM_PARAM_INDEX  9

#define MAX_OPEN_TABLES         9

#define DEFAULT_SECURITY_LEVEL  0

#define NUM_ASSIGN_LAYERS       20

#include "main.h"
