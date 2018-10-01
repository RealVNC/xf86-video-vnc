/*
 * Copyright (C) 2018, RealVNC Ltd.
 *
 * This code is based on the X.Org dummy video driver with the following copyrights:
 *
 * Copyright 2002, SuSE Linux AG, Author: Egbert Eich
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"
#include "cursorstr.h"
/* Driver specific headers */
#include "vnc.h"

static void
vncShowCursor(ScrnInfoPtr pScrn)
{
    VNCPtr dPtr = VNCPTR(pScrn);

    /* turn cursor on */
    dPtr->VncHWCursorShown = TRUE;    
}

static void
vncHideCursor(ScrnInfoPtr pScrn)
{
    VNCPtr dPtr = VNCPTR(pScrn);

    /*
     * turn cursor off 
     *
     */
    dPtr->VncHWCursorShown = FALSE;
}

#define MAX_CURS 64

static void
vncSetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    VNCPtr dPtr = VNCPTR(pScrn);

    dPtr->cursorX = x;
    dPtr->cursorY = y;
}

static void
vncSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    VNCPtr dPtr = VNCPTR(pScrn);
    
    dPtr->cursorFG = fg;
    dPtr->cursorBG = bg;
}

static void
vncLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src)
{
}

static Bool
vncUseHWCursor(ScreenPtr pScr, CursorPtr pCurs)
{
    VNCPtr dPtr = VNCPTR(xf86ScreenToScrn(pScr));
    return(!dPtr->swCursor);
}

#if 0
static unsigned char*
vncRealizeCursor(xf86CursorInfoPtr infoPtr, CursorPtr pCurs)
{
    return NULL;
}
#endif

Bool
VNCCursorInit(ScreenPtr pScreen)
{
    VNCPtr dPtr = VNCPTR(xf86ScreenToScrn(pScreen));

    xf86CursorInfoPtr infoPtr;
    infoPtr = xf86CreateCursorInfoRec();
    if(!infoPtr) return FALSE;

    dPtr->CursorInfo = infoPtr;

    infoPtr->MaxHeight = 64;
    infoPtr->MaxWidth = 64;
    infoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP;

    infoPtr->SetCursorColors = vncSetCursorColors;
    infoPtr->SetCursorPosition = vncSetCursorPosition;
    infoPtr->LoadCursorImage = vncLoadCursorImage;
    infoPtr->HideCursor = vncHideCursor;
    infoPtr->ShowCursor = vncShowCursor;
    infoPtr->UseHWCursor = vncUseHWCursor;
/*     infoPtr->RealizeCursor = vncRealizeCursor; */
    
    return(xf86InitCursor(pScreen, infoPtr));
}



