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

/* All drivers initialising the SW cursor need this */
#include "mipointer.h"

/* All drivers using the mi colormap manipulation need this */
#include "micmap.h"

#include <X11/Xatom.h>
#include "property.h"
#include "xf86cmap.h"
#include "xf86fbman.h"
#include "fb.h"
#include "picturestr.h"
#include "xf86Crtc.h"

/*
 * Driver data structures.
 */
#include "vnc.h"

/* These need to be checked */
#include <X11/X.h>
#include <X11/Xproto.h>
#include "scrnintstr.h"
#include "servermd.h"

/* Mandatory functions */
static const OptionInfoRec *	VNCAvailableOptions(int chipid, int busid);
static void     VNCIdentify(int flags);
static Bool     VNCProbe(DriverPtr drv, int flags);
static Bool     VNCPreInit(ScrnInfoPtr pScrn, int flags);
static Bool     VNCScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool     VNCEnterVT(VT_FUNC_ARGS_DECL);
static void     VNCLeaveVT(VT_FUNC_ARGS_DECL);
static Bool     VNCCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static Bool     VNCCreateWindow(WindowPtr pWin);
static void     VNCFreeScreen(FREE_SCREEN_ARGS_DECL);
static ModeStatus VNCValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                 Bool verbose, int flags);
static Bool	VNCSaveScreen(ScreenPtr pScreen, int mode);

/* Internally used functions */
static Bool	vncDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
				pointer ptr);

#define VNC_VERSION 4000
#define VNC_NAME "VNC"
#define VNC_DRIVER_NAME "vnc"

#define VNC_MAJOR_VERSION PACKAGE_VERSION_MAJOR
#define VNC_MINOR_VERSION PACKAGE_VERSION_MINOR
#define VNC_PATCHLEVEL PACKAGE_VERSION_PATCHLEVEL

#define VNC_MAX_WIDTH 32767
#define VNC_MAX_HEIGHT 32767
#define VNC_MAX_OUTPUTS 10

/*
 * This contains the functions needed by the server after loading the driver
 * module.  It must be supplied, and gets passed back by the SetupProc
 * function in the dynamic case.  In the static case, a reference to this
 * is compiled in, and this requires that the name of this DriverRec be
 * an upper-case version of the driver name.
 */

_X_EXPORT DriverRec VNC = {
    VNC_VERSION,
    VNC_DRIVER_NAME,
    VNCIdentify,
    VNCProbe,
    VNCAvailableOptions,
    NULL,
    0,
    vncDriverFunc
};

static SymTabRec VNCChipsets[] = {
    { VNC_CHIP,   "vnc" },
    { -1,		 NULL }
};

typedef enum {
    OPTION_SW_CURSOR,
    OPTION_NUM_OUTPUTS
} VNCOpts;

static const OptionInfoRec VNCOptions[] = {
    { OPTION_SW_CURSOR,	  "SWcursor",	OPTV_BOOLEAN,	{0}, FALSE },
    { OPTION_NUM_OUTPUTS, "NumOutputs",	OPTV_INTEGER,	{0}, FALSE },
    { -1,                  NULL,           OPTV_NONE,	{0}, FALSE }
};

#ifdef XFree86LOADER

static MODULESETUPPROTO(vncSetup);

static XF86ModuleVersionInfo vncVersRec =
{
	"vnc",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	VNC_MAJOR_VERSION, VNC_MINOR_VERSION, VNC_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

/*
 * This is the module init data.
 * Its name has to be the driver name followed by ModuleData
 */
_X_EXPORT XF86ModuleData vncModuleData = { &vncVersRec, vncSetup, NULL };

static pointer
vncSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if (!setupDone) {
	setupDone = TRUE;
        xf86AddDriver(&VNC, module, HaveDriverFuncs);

	/*
	 * Modules that this driver always requires can be loaded here
	 * by calling LoadSubModule().
	 */

	/*
	 * The return value must be non-NULL on success even though there
	 * is no TearDownProc.
	 */
	return (pointer)1;
    } else {
	if (errmaj) *errmaj = LDR_ONCEONLY;
	return NULL;
    }
}

#endif /* XFree86LOADER */

static Bool
size_valid(ScrnInfoPtr pScrn, int width, int height)
{
    /* Guard against invalid parameters */
    if (width == 0 || height == 0 ||
        width > VNC_MAX_WIDTH || height > VNC_MAX_HEIGHT)
        return FALSE;

    /* videoRam is in kb, divide first to avoid 32-bit int overflow */
    if ((width*height+1023)/1024*pScrn->bitsPerPixel/8 > pScrn->videoRam)
        return FALSE;

    return TRUE;
}

static void*
realloc_fb(ScrnInfoPtr pScrn, void* current)
{
    int fbBytes = pScrn->virtualX * pScrn->virtualY * pScrn->bitsPerPixel / 8;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Setting fb to %d x %d (%d B)\n",
	       pScrn->virtualX, pScrn->virtualY, fbBytes);
    void* pixels = current ? realloc(current, fbBytes) : malloc(fbBytes);
    if (!pixels)
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to (re)alloc fb\n");
    return pixels;
}

static Bool
vnc_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
    int old_width, old_height;
    old_width = pScrn->virtualX;
    old_height = pScrn->virtualY;

    if (size_valid(pScrn, width, height)) {
        PixmapPtr rootPixmap;
        ScreenPtr pScreen = pScrn->pScreen;

        pScrn->virtualX = width;
        pScrn->virtualY = height;

        rootPixmap = pScreen->GetScreenPixmap(pScreen);
	void* pixels = realloc_fb(pScrn, rootPixmap->devPrivate.ptr);
	if (!pixels)
	  return FALSE;
	if (!pScreen->ModifyPixmapHeader(rootPixmap, width, height,
                                         -1, -1, -1, pixels)) {
            pScrn->virtualX = old_width;
            pScrn->virtualY = old_height;
            return FALSE;
        }
        pScrn->displayWidth = pScrn->virtualX * (pScrn->bitsPerPixel / 8);

        return TRUE;
    } else {
        return FALSE;
    }
}

static const xf86CrtcConfigFuncsRec vnc_xf86crtc_config_funcs = {
    vnc_xf86crtc_resize
};

static xf86OutputStatus
vnc_output_detect(xf86OutputPtr output)
{
    return XF86OutputStatusConnected;
}

static int
vnc_output_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    return MODE_OK;
}

static DisplayModePtr add_mode(DisplayModePtr modes, int cx, int cy)
{
    DisplayModePtr m = xnfcalloc(sizeof(DisplayModeRec), 1);
    DisplayModePtr last;
    
    char modeName[256];
    sprintf(modeName, "%ux%u", cx, cy);
    m->name = xnfstrdup(modeName);      
  
    m->status = MODE_OK;
    m->type = M_T_BUILTIN;
    m->HDisplay  = cx;
    m->HSyncStart = m->HDisplay + 2;
    m->HSyncEnd = m->HDisplay + 4;
    m->HTotal = m->HDisplay + 6;
    m->VDisplay = cy;
    m->VSyncStart = m->VDisplay + 2;
    m->VSyncEnd = m->VDisplay + 4;
    m->VTotal = m->VDisplay + 6;
    m->Clock = m->HTotal * m->VTotal * 60 / 1000; /* kHz */ 
    
    m->next = 0;
    m->prev = 0;
    if (!modes) return m;
    
    /* Add to existing list */
    for (last = modes; last->next != 0; last = last->next) {};
    m->prev = last;
    last->next = m;
    return modes;
}

static DisplayModePtr
vnc_output_get_modes(xf86OutputPtr output)
{
    DisplayModePtr m = 0;
    m = add_mode(m, 1024, 768);
    return m;
}

static void
vnc_output_dpms(xf86OutputPtr output, int dpms)
{
    return;
}

static const xf86OutputFuncsRec vnc_output_funcs = {
    .detect = vnc_output_detect,
    .mode_valid = vnc_output_mode_valid,
    .get_modes = vnc_output_get_modes,
    .dpms = vnc_output_dpms,
};

static Bool
vnc_crtc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
			  Rotation rotation, int x, int y)
{
    crtc->mode = *mode;
    crtc->x = x;
    crtc->y = y;
    crtc->rotation = rotation;

    return TRUE;
}

static void
vnc_crtc_dpms(xf86CrtcPtr output, int dpms)
{
    return;
}

static const xf86CrtcFuncsRec vnc_crtc_funcs = {
    .set_mode_major = vnc_crtc_set_mode_major,
    .dpms = vnc_crtc_dpms,
};

static Bool
VNCGetRec(ScrnInfoPtr pScrn)
{
    /*
     * Allocate a VNCRec, and hook it into pScrn->driverPrivate.
     * pScrn->driverPrivate is initialised to NULL, so we can check if
     * the allocation has already been done.
     */
    if (pScrn->driverPrivate != NULL)
	return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(VNCRec), 1);

    if (pScrn->driverPrivate == NULL)
	return FALSE;
    return TRUE;
}

static void
VNCFreeRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate == NULL)
	return;
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static const OptionInfoRec *
VNCAvailableOptions(int chipid, int busid)
{
    return VNCOptions;
}

/* Mandatory */
static void
VNCIdentify(int flags)
{
    xf86PrintChipsets(VNC_NAME, "Driver for Vnc chipsets",
			VNCChipsets);
}

/* Mandatory */
static Bool
VNCProbe(DriverPtr drv, int flags)
{
    Bool foundScreen = FALSE;
    int numDevSections, numUsed;
    GDevPtr *devSections;
    int i;

    if (flags & PROBE_DETECT)
	return FALSE;
    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    if ((numDevSections = xf86MatchDevice(VNC_DRIVER_NAME,
					  &devSections)) <= 0) {
	return FALSE;
    }

    numUsed = numDevSections;

    if (numUsed > 0) {

	for (i = 0; i < numUsed; i++) {
	    ScrnInfoPtr pScrn = NULL;
	    int entityIndex = 
		xf86ClaimNoSlot(drv,VNC_CHIP,devSections[i],TRUE);
	    /* Allocate a ScrnInfoRec and claim the slot */
	    if ((pScrn = xf86AllocateScreen(drv,0 ))) {
		   xf86AddEntityToScreen(pScrn,entityIndex);
		    pScrn->driverVersion = VNC_VERSION;
		    pScrn->driverName    = VNC_DRIVER_NAME;
		    pScrn->name          = VNC_NAME;
		    pScrn->Probe         = VNCProbe;
		    pScrn->PreInit       = VNCPreInit;
		    pScrn->ScreenInit    = VNCScreenInit;
		    pScrn->SwitchMode    = VNCSwitchMode;
		    pScrn->AdjustFrame   = VNCAdjustFrame;
		    pScrn->EnterVT       = VNCEnterVT;
		    pScrn->LeaveVT       = VNCLeaveVT;
		    pScrn->FreeScreen    = VNCFreeScreen;
		    pScrn->ValidMode     = VNCValidMode;

		    foundScreen = TRUE;
	    }
	}
    }    

    free(devSections);

    return foundScreen;
}

# define RETURN					\
    { VNCFreeRec(pScrn);			\
	return FALSE;				\
    }

/* Mandatory */
Bool
VNCPreInit(ScrnInfoPtr pScrn, int flags)
{
    ClockRangePtr clockRanges;
    int i;
    VNCPtr dPtr;
    int maxClock = 300000;
    GDevPtr device = xf86GetEntityInfo(pScrn->entityList[0])->device;
    xf86OutputPtr output[VNC_MAX_OUTPUTS];
    xf86CrtcPtr crtc[VNC_MAX_OUTPUTS];

    if (flags & PROBE_DETECT) 
	return TRUE;

    /* Allocate the VncRec driverPrivate */
    if (!VNCGetRec(pScrn)) {
	return FALSE;
    }
    
    dPtr = VNCPTR(pScrn);

    pScrn->chipset = (char *)xf86TokenToString(VNCChipsets,
					       VNC_CHIP);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Chipset is a VNC\n");
    
    pScrn->monitor = pScrn->confScreen->monitor;

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0,  Support24bppFb | Support32bppFb))
	return FALSE;
    else {
	/* Check that the returned depth is one we support */
	switch (pScrn->depth) {
	case 8:
	case 15:
	case 16:
	case 24:
	case 30:
	    break;
	default:
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Given depth (%d) is not supported by this driver\n",
		       pScrn->depth);
	    return FALSE;
	}
    }

    xf86PrintDepthBpp(pScrn);
    if (pScrn->depth == 8)
	pScrn->rgbBits = 8;

    /*
     * This must happen after pScrn->display has been set because
     * xf86SetWeight references it.
     */
    if (pScrn->depth > 8) {
	/* The defaults are OK for us */
	rgb zeros = {0, 0, 0};

	if (!xf86SetWeight(pScrn, zeros, zeros)) {
	    return FALSE;
	} else {
	    /* XXX check that weight returned is supported */
	    ;
	}
    }

    if (!xf86SetDefaultVisual(pScrn, -1)) 
	return FALSE;

    if (pScrn->depth > 1) {
	Gamma zeros = {0.0, 0.0, 0.0};

	if (!xf86SetGamma(pScrn, zeros))
	    return FALSE;
    }

    xf86SetDpi(pScrn, 96, 96);

    xf86CollectOptions(pScrn, device->options);
    /* Process the options */
    if (!(dPtr->Options = malloc(sizeof(VNCOptions))))
	return FALSE;
    memcpy(dPtr->Options, VNCOptions, sizeof(VNCOptions));

    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, dPtr->Options);

    xf86GetOptValBool(dPtr->Options, OPTION_SW_CURSOR,&dPtr->swCursor);
    
    dPtr->numOutputs = 1;
    xf86GetOptValInteger(dPtr->Options, OPTION_NUM_OUTPUTS,&dPtr->numOutputs);
    if (dPtr->numOutputs > VNC_MAX_OUTPUTS) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Too many outputs (maximum is %u)\n",
		   VNC_MAX_OUTPUTS);
	RETURN;
    }

    if (device->videoRam != 0) {
	pScrn->videoRam = device->videoRam;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "VideoRAM: %d kByte\n",
		   pScrn->videoRam);
    } else {
	pScrn->videoRam = 4096;
	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "VideoRAM: %d kByte\n",
		   pScrn->videoRam);
    }

    if (device->dacSpeeds[0] != 0) {
	maxClock = device->dacSpeeds[0];
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Max Clock: %d kHz\n",
		   maxClock);
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Max Clock: %d kHz\n",
		   maxClock);
    }

    xf86CrtcConfigInit(pScrn, &vnc_xf86crtc_config_funcs);

    for (i=0; i<dPtr->numOutputs; ++i) {
	crtc[i] = xf86CrtcCreate(pScrn, &vnc_crtc_funcs);
	crtc[i]->driver_private = (void *)(uintptr_t)i;
	
	char outputName[256];
	snprintf(outputName, sizeof(outputName), "vnc-%u", i);
	output[i] = xf86OutputCreate (pScrn, &vnc_output_funcs, outputName);
	
	xf86OutputUseScreenMonitor(output[i], TRUE);
	
	output[i]->possible_crtcs = 1 << i;
	output[i]->possible_clones = 0;
	output[i]->driver_private = (void *)(uintptr_t)i;
    }

    xf86CrtcSetSizeRange(pScrn, 256, 256, VNC_MAX_WIDTH, VNC_MAX_HEIGHT);

    xf86InitialConfiguration(pScrn, TRUE);

    if (pScrn->modes == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
	return FALSE;
    }

    /* Set the current mode to the first in the list */
    pScrn->currentMode = pScrn->modes;

    for (i=0; i<dPtr->numOutputs; ++i) {
	/* Set default mode in CRTC */
	crtc[i]->funcs->set_mode_major(crtc[i], pScrn->currentMode, RR_Rotate_0, 0, 0);
    }

    /* We have no contiguous physical fb in physical memory */
    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
	return FALSE;
    }

    if (!dPtr->swCursor) {
	if (!xf86LoadSubModule(pScrn, "ramdac"))
	    return FALSE;
    }
    
    return TRUE;
}
#undef RETURN

/* Mandatory */
static Bool
VNCEnterVT(VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    
    VNCAdjustFrame(ADJUST_FRAME_ARGS(pScrn, pScrn->frameX0, pScrn->frameY0));

    return TRUE;
}

/* Mandatory */
static void
VNCLeaveVT(VT_FUNC_ARGS_DECL)
{
}

static void
VNCLoadPalette(
   ScrnInfoPtr pScrn,
   int numColors,
   int *indices,
   LOCO *colors,
   VisualPtr pVisual
){
   int i, index, shift, Gshift;
   VNCPtr dPtr = VNCPTR(pScrn);

   switch(pScrn->depth) {
   case 15:	
	shift = Gshift = 1;
	break;
   case 16:
	shift = 0; 
        Gshift = 0;
	break;
   default:
	shift = Gshift = 0;
	break;
   }

   for(i = 0; i < numColors; i++) {
       index = indices[i];
       dPtr->colors[index].red = colors[index].red << shift;
       dPtr->colors[index].green = colors[index].green << Gshift;
       dPtr->colors[index].blue = colors[index].blue << shift;
   } 

}

static ScrnInfoPtr VNCScrn; /* static-globalize it */

/* Mandatory */
static Bool
VNCScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScrn;
    VNCPtr dPtr;
    int ret;
    VisualPtr visual;
    void *pixels;

    /*
     * we need to get the ScrnInfoRec for this screen, so let's allocate
     * one first thing
     */
    pScrn = xf86ScreenToScrn(pScreen);
    dPtr = VNCPTR(pScrn);
    VNCScrn = pScrn;

    /*
     * Reset visual list.
     */
    miClearVisualTypes();
    
    /* Setup the visuals we support. */
    
    if (!miSetVisualTypes(pScrn->depth,
      		      miGetDefaultVisualMask(pScrn->depth),
		      pScrn->rgbBits, pScrn->defaultVisual))
         return FALSE;

    if (!miSetPixmapDepths ()) return FALSE;

    pScrn->displayWidth = pScrn->virtualX;

    pixels = realloc_fb(pScrn, 0);
    if (!pixels)
      return FALSE;

    /*
     * Call the framebuffer layer's ScreenInit function, and fill in other
     * pScreen fields.
     */
    ret = fbScreenInit(pScreen, pixels,
			    pScrn->virtualX, pScrn->virtualY,
			    pScrn->xDpi, pScrn->yDpi,
			    pScrn->displayWidth, pScrn->bitsPerPixel);
    if (!ret)
	return FALSE;

    if (pScrn->depth > 8) {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
	    if ((visual->class | DynamicClass) == DirectColor) {
		visual->offsetRed = pScrn->offset.red;
		visual->offsetGreen = pScrn->offset.green;
		visual->offsetBlue = pScrn->offset.blue;
		visual->redMask = pScrn->mask.red;
		visual->greenMask = pScrn->mask.green;
		visual->blueMask = pScrn->mask.blue;
	    }
	}
    }
    
    /* must be after RGB ordering fixed */
    fbPictureInit(pScreen, 0, 0);

    xf86SetBlackWhitePixels(pScreen);

    if (dPtr->swCursor)
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Using Software Cursor.\n");

    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);
	
    /* Initialise cursor functions */
    miDCInitialize (pScreen, xf86GetPointerScreenFuncs());


    if (!dPtr->swCursor) {
      /* HW cursor functions */
      if (!VNCCursorInit(pScreen)) {
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		     "Hardware cursor initialization failed\n");
	  return FALSE;
      }
    }
    
    /* Initialise default colourmap */
    if(!miCreateDefColormap(pScreen))
	return FALSE;

    if (!xf86HandleColormaps(pScreen, 1024, pScrn->rgbBits,
                         VNCLoadPalette, NULL, 
                         CMAP_PALETTED_TRUECOLOR 
			     | CMAP_RELOAD_ON_MODE_SWITCH))
	return FALSE;

    if (!xf86CrtcScreenInit(pScreen))
        return FALSE;

    if (!xf86SetDesiredModes(pScrn)) {
        return FALSE;
    }

    pScreen->SaveScreen = VNCSaveScreen;

    
    /* Wrap the current CloseScreen function */
    dPtr->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = VNCCloseScreen;

    /* Wrap the current CreateWindow function */
    dPtr->CreateWindow = pScreen->CreateWindow;
    pScreen->CreateWindow = VNCCreateWindow;

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1) {
	xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }

    return TRUE;
}

/* Mandatory */
Bool
VNCSwitchMode(SWITCH_MODE_ARGS_DECL)
{
    return TRUE;
}

/* Mandatory */
void
VNCAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
}

/* Mandatory */
static Bool
VNCCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    VNCPtr dPtr = VNCPTR(pScrn);

    free(pScreen->GetScreenPixmap(pScreen)->devPrivate.ptr);

    if (dPtr->CursorInfo)
	xf86DestroyCursorInfoRec(dPtr->CursorInfo);

    pScrn->vtSema = FALSE;
    pScreen->CloseScreen = dPtr->CloseScreen;
    return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

/* Optional */
static void
VNCFreeScreen(FREE_SCREEN_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    VNCFreeRec(pScrn);
}

static Bool
VNCSaveScreen(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

/* Optional */
static ModeStatus
VNCValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode, Bool verbose, int flags)
{
    return(MODE_OK);
}

Atom VNC_PROP  = 0;
#define  VNC_PROP_NAME  "VNC_DRV_VERSION"

static Bool
VNCCreateWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    VNCPtr dPtr = VNCPTR(VNCScrn);
    WindowPtr pWinRoot;
    int ret;

    pScreen->CreateWindow = dPtr->CreateWindow;
    ret = pScreen->CreateWindow(pWin);
    dPtr->CreateWindow = pScreen->CreateWindow;
    pScreen->CreateWindow = VNCCreateWindow;

    if(ret != TRUE)
	return(ret);
	
    if(dPtr->prop == FALSE) {
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 8
        pWinRoot = WindowTable[VNCScrn->pScreen->myNum];
#else
        pWinRoot = VNCScrn->pScreen->root;
#endif
        if (! ValidAtom(VNC_PROP))
            VNC_PROP = MakeAtom(VNC_PROP_NAME, strlen(VNC_PROP_NAME), 1);

        ret = dixChangeWindowProperty(serverClient, pWinRoot, VNC_PROP,
                                      XA_STRING, 8, PropModeReplace,
                                      (int)strlen(VERSION), (pointer)VERSION, FALSE);
	if( ret != Success)
	    ErrorF("Could not set VNC_DRV_VERSION root window property");
        dPtr->prop = TRUE;
	
	return TRUE;
    }
    return TRUE;
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

static Bool
vncDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    CARD32 *flag;
    
    switch (op) {
	case GET_REQUIRED_HW_INTERFACES:
	    flag = (CARD32*)ptr;
	    (*flag) = HW_SKIP_CONSOLE;
	    return TRUE;
	default:
	    return FALSE;
    }
}
