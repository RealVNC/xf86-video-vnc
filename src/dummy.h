
/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"

#ifdef XvExtension
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#endif
#include <string.h>

#include "compat-api.h"

/* Supported chipsets */
typedef enum {
    DUMMY_CHIP
} DUMMYType;

/* function prototypes */

extern Bool DUMMYSwitchMode(SWITCH_MODE_ARGS_DECL);
extern void DUMMYAdjustFrame(ADJUST_FRAME_ARGS_DECL);

/* in dummy_cursor.c */
extern Bool DUMMYCursorInit(ScreenPtr pScrn);
extern void DUMMYShowCursor(ScrnInfoPtr pScrn);
extern void DUMMYHideCursor(ScrnInfoPtr pScrn);

/* globals */
typedef struct _color
{
    int red;
    int green;
    int blue;
} dummy_colors;

typedef struct dummyRec 
{
    /* options */
    OptionInfoPtr Options;
    Bool swCursor;
    int numOutputs;
    /* proc pointer */
    CloseScreenProcPtr CloseScreen;
    xf86CursorInfoPtr CursorInfo;

    Bool DummyHWCursorShown;
    int cursorX, cursorY;
    int cursorFG, cursorBG;

    dummy_colors colors[1024];
    Bool        (*CreateWindow)() ;     /* wrapped CreateWindow */
    Bool prop;
} DUMMYRec, *DUMMYPtr;

/* The privates of the DUMMY driver */
#define DUMMYPTR(p)	((DUMMYPtr)((p)->driverPrivate))

