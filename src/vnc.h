/*
 * Copyright (C) 2018, RealVNC Ltd.
 *
 * This code is based on the X.Org dummy video driver with the following copyrights:
 *
 * Copyright 2002, SuSE Linux AG, Author: Egbert Eich
 */

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
    VNC_CHIP
} VNCType;

/* function prototypes */

extern Bool VNCSwitchMode(SWITCH_MODE_ARGS_DECL);
extern void VNCAdjustFrame(ADJUST_FRAME_ARGS_DECL);

/* in vnc_cursor.c */
extern Bool VNCCursorInit(ScreenPtr pScrn);
extern void VNCShowCursor(ScrnInfoPtr pScrn);
extern void VNCHideCursor(ScrnInfoPtr pScrn);

/* globals */
typedef struct _color
{
    int red;
    int green;
    int blue;
} vnc_colors;

typedef struct vncRec 
{
    /* options */
    OptionInfoPtr Options;
    Bool swCursor;
    int numOutputs;
    /* proc pointer */
    CloseScreenProcPtr CloseScreen;
    xf86CursorInfoPtr CursorInfo;

    Bool VncHWCursorShown;
    int cursorX, cursorY;
    int cursorFG, cursorBG;

    vnc_colors colors[1024];
    Bool        (*CreateWindow)() ;     /* wrapped CreateWindow */
    Bool prop;
} VNCRec, *VNCPtr;

/* The privates of the VNC driver */
#define VNCPTR(p)	((VNCPtr)((p)->driverPrivate))

