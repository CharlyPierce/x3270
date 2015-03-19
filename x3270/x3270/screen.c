/*
 * Copyright (c) 1993-2015 Paul Mattes.
 * Copyright (c) 1990, Jeff Sparkes.
 * Copyright (c) 1989, Georgia Tech Research Corporation (GTRC), Atlanta,
 *  GA 30332.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes, Jeff Sparkes, GTRC nor their
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES, JEFF SPARKES AND GTRC "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL PAUL MATTES, JEFF SPARKES OR
 * GTRC BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	screen.c
 *		This module handles the X display.  It has been extensively
 *		optimized to minimize X drawing operations.
 */

#include "globals.h"
#include "xglobals.h"

#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Shell.h>
#include <X11/Composite.h>
#include <X11/Xaw/Dialog.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include "Husk.h"
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include "3270ds.h"
#include "appres.h"
#include "screen.h"
#include "ctlr.h"
#include "cg.h"
#include "resources.h"
#include "toggles.h"

#include "actions.h"
#include "charset.h"
#include "ctlrc.h"
#include "display8.h"
#include "display_charsets.h"
#include "display_charsets_dbcs.h"
#include "host.h"
#include "keymap.h"
#include "kybd.h"
#include "lazya.h"
#include "nvt.h"
#include "popups.h"
#include "save.h"
#include "screen.h"
#include "scroll.h"
#include "see.h"
#include "status.h"
#include "tables.h"
#include "trace.h"
#include "unicodec.h"
#include "unicode_dbcs.h"
#include "util.h"
#include "xactions.h"
#include "xappres.h"
#include "xio.h"
#include "xkeypad.h"
#include "xmenubar.h"
#include "xsave.h"
#include "xscreen.h"
#include "xscroll.h"
#include "xstatus.h"
#include "xpopups.h"
#include "xtables.h"

#if defined(HAVE_SYS_SELECT_H) /*[*/
#include <sys/select.h>		/* fd_set declaration */
#endif /*]*/

#include "x3270.bm"
#include "wait.bm"

#define SCROLLBAR_WIDTH	15

#define NO_BANG(s)	(((s)[0] == '!')? (s)+1: (s))

#if !defined(NBBY) /*[*/
#define NBBY 8
#endif /*]*/
#define BPW	(NBBY * sizeof(unsigned long))

#define MAX_FONTS	50000

#define SELECTED(baddr)		(selected[(baddr)/8] & (1 << ((baddr)%8)))
#define SET_SELECT(baddr)	(selected[(baddr)/8] |= (1 << ((baddr)%8)))

/* Globals */
Dimension       main_width;
bool         scrollbar_changed = false;
bool         model_changed = false;
bool		efont_changed = false;
bool		oversize_changed = false;
bool		scheme_changed = false;
Pixel           keypadbg_pixel;
bool         flipped = false;
Pixmap          x3270_icon;
bool		shifted = false;
struct font_list *font_list = (struct font_list *) NULL;
int             font_count = 0;
char	       *efontname;
char           *efont_charset;
char           *efont_charset_dbcs;
bool		efont_matches = true;
char	       *full_efontname;
char	       *full_efontname_dbcs;
bool		visible_control = false;
unsigned	fixed_width, fixed_height;
int		hhalo = HHALO, vhalo = VHALO;

#define gray_width 2
#define gray_height 2
static char gray_bits[] = { 0x01, 0x02 };

/* Statics */
static unsigned char  *selected;	/* selection bitmap */
static bool	allow_resize;
static Dimension main_height;
static union sp *temp_image;	/* temporary for X display */
static Pixel	colorbg_pixel;
static bool	crosshair_enabled = true;
static bool  cursor_displayed = false;
static bool  cursor_enabled = true;
static bool  cursor_blink_pending = false;
static XtIntervalId cursor_blink_id;
static int	field_colors[4];
static bool  in_focus = false;
static bool  line_changed = false;
static bool  cursor_changed = false;
static bool  iconic = false;
static Widget   container;
static Widget   scrollbar;
static Dimension menubar_height;
static Dimension keypad_height;
static Dimension keypad_xwidth;
static Dimension container_width;
static Dimension cwidth_nkp;
static Dimension container_height;
static Dimension scrollbar_width;
static char    *aicon_text = NULL;
static XFontStruct *ailabel_font;
static Dimension aicon_label_height = 0;
static GC       ailabel_gc;
static Pixel    cpx[16];
static bool  cpx_done[16];
static Pixel    normal_pixel;
static Pixel    select_pixel;
static Pixel    bold_pixel;
static Pixel    selbg_pixel;
static Pixel    cursor_pixel;
static bool  text_blinking_on = true;
static bool  text_blinkers_exist = false;
static bool  text_blink_scheduled = false;
static XtIntervalId text_blink_id;
static XtTranslations screen_t00 = NULL;
static XtTranslations screen_t0 = NULL;
static XtTranslations container_t00 = NULL;
static XtTranslations container_t0 = NULL;
static XChar2b *rt_buf = (XChar2b *) NULL;
static char    *color_name[16] = {
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL
};
static bool	configure_ticking = false;
static XtIntervalId configure_id;

static Pixmap   inv_icon;
static Pixmap   wait_icon;
static Pixmap   inv_wait_icon;
static bool  icon_inverted = false;
static Widget	icon_shell;

static struct font_list *font_last = (struct font_list *) NULL;

static struct {
    Font font;
    XFontStruct *font_struct;
    bool unicode;
    int char_height;
    int char_width;
    int ascent;
    int descent;
    int xtra_width;
    int d16_ix;
} dbcs_font;
static void xim_init(void);
XIM im;
XIC ic;
bool xim_error = false;
char *locale_name = NULL;
int ovs_offset = 1;
typedef struct {
	XIMStyle style;
	char *description;
} im_style_t;
static XIMStyle style;
char ic_focus;
static void send_spot_loc(void);

/* Globals for undoing reconfigurations. */
static enum {
    REDO_NONE,
    REDO_FONT,
    REDO_MODEL,
    REDO_KEYPAD,
    REDO_SCROLLBAR,
    REDO_RESIZE
} screen_redo = REDO_NONE;
static char *redo_old_font = NULL;
static int redo_old_model;
static int redo_old_ov_cols;
static int redo_old_ov_rows;

static unsigned char blank_map[32];
#define BKM_SET(n)	blank_map[(n)/8] |= 1 << ((n)%8)
#define BKM_ISSET(n)	((blank_map[(n)/8] & (1 << ((n)%8))) != 0)

enum fallback_color { FB_WHITE, FB_BLACK };
static enum fallback_color ibm_fb = FB_WHITE;

static char *default_display_charset = "3270cg-1a,3270cg-1,iso8859-1";
static char *required_display_charsets;

#define CROSSABLE	(toggled(CROSSHAIR) && IN_3270 && \
			 cursor_enabled && crosshair_enabled)
#define CROSSED(b)	((BA_TO_COL(b) == cursor_col) || \
			 (BA_TO_ROW(b) == cursor_row))

/*
 * The screen state structure.  This structure is swapped whenever we switch
 * between normal and active-iconic states.
 */
#define NGCS	16
struct sstate {
	Widget          widget;	/* the widget */
	Window          window;	/* the window */
	union sp       *image;	/* what's on the X display */
	int             cursor_daddr;	/* displayed cursor address */
	bool         exposed_yet;	/* have we been exposed yet? */
	bool         overstrike;	/* are we overstriking? */
	Dimension       screen_width;	/* screen dimensions in pixels */
	Dimension       screen_height;
	GC              gc[NGCS * 2],	/* standard, inverted GCs */
	                selgc[NGCS],	/* color selected text GCs */
	                mcgc,		/* monochrome block cursor GC */
	                ucgc,		/* unique-cursor-color cursor GC */
	                invucgc,	/* inverse ucgc */
			clrselgc;	/* selected clearing GC */
	int             char_height;
	int             char_width;
	Font		fid;
	XFontStruct	*font;
	int		ascent;
	int		descent;
	int		xtra_width;
	bool         standard_font;
	bool		extended_3270font;
	bool         font_8bit;
	bool		font_16bit;
	bool		funky_font;
	bool         obscured;
	bool         copied;
	int		d8_ix;
	unsigned long	odd_width[256 / BPW];
	unsigned long	odd_lbearing[256 / BPW];
};
static struct sstate nss;
static struct sstate iss;
static struct sstate *ss = &nss;

#define	INIT_ODD(odd)	(void) memset(odd, '\0', sizeof(odd))
#define SET_ODD(odd, n)	(odd)[(n) / BPW] |= 1 << ((n) % BPW)
#define IS_ODD(odd, n)	((odd)[(n) / BPW] & 1 << ((n) % BPW))

#define DEFAULT_CHAR(f) (((f)->default_char >= (f)->min_char_or_byte2 && \
			  (f)->default_char <= (f)->max_char_or_byte2)? \
			    (f)->default_char: 32)
#define WHICH_CHAR(f, n) (((n) < (f)->min_char_or_byte2 || \
			   (n) > (f)->max_char_or_byte2)? \
			    DEFAULT_CHAR(f) : (n))
#define PER_CHAR(f, n)	((f)->per_char[WHICH_CHAR(f, n) - (f)->min_char_or_byte2])

/* Globals based on nss, used mostly by status and select routines. */
Widget         *screen = &nss.widget;
Window         *screen_window = &nss.window;
int            *char_width = &nss.char_width;
int            *char_height = &nss.char_height;
int            *ascent = &nss.ascent;
int            *descent = &nss.descent;
bool        *standard_font = &nss.standard_font;
bool        *font_8bit = &nss.font_8bit;
bool        *font_16bit = &nss.font_16bit;
bool        *extended_3270font = &nss.extended_3270font;
bool        *funky_font = &nss.funky_font;
int            *xtra_width = &nss.xtra_width;
Font           *fid = &nss.fid;

/* Mouse-cursor state */
enum mcursor_state { LOCKED, NORMAL, WAIT };
static enum mcursor_state mcursor_state = LOCKED;
static enum mcursor_state icon_cstate = NORMAL;

/* Dumb font cache. */
typedef struct dfc {
    	struct dfc *next;
    	char *name;
	char *weight;
	int points;
	char *spacing;
	char *charset;
	bool good;
} dfc_t;

static void aicon_init(void);
static void aicon_reinit(unsigned cmask);
static void screen_focus(bool in);
static void make_gc_set(struct sstate *s, int i, Pixel fg, Pixel bg);
static void make_gcs(struct sstate *s);
static void put_cursor(int baddr, bool on);
static void resync_display(union sp *buffer, int first, int last);
static void draw_fields(union sp *buffer, int first, int last);
static void render_text(union sp *buffer, int baddr, int len,
    bool block_cursor, union sp *attrs);
static void cursor_pos(void);
static void cursor_on(void);
static void schedule_cursor_blink(void);
static void schedule_text_blink(void);
static void inflate_screen(void);
static int fa_color(unsigned char fa);
static bool cursor_off(void);
static void draw_aicon_label(void);
static void set_mcursor(void);
static void scrollbar_init(bool is_reset);
static void init_rsfonts(char *charset_name);
static void allocate_pixels(void);
static int fl_baddr(int baddr);
static GC get_gc(struct sstate *s, int color);
static GC get_selgc(struct sstate *s, int color);
static void default_color_scheme(void);
static bool xfer_color_scheme(char *cs, bool do_popup);
static void set_font_globals(XFontStruct *f, const char *ef, const char *fef,
    Font ff, bool is_dbcs);
static void screen_connect(bool ignored);
static void configure_stable(XtPointer closure, XtIntervalId *id);
static void cancel_blink(void);
static void render_blanks(int baddr, int height, union sp *buffer);
static void resync_text(int baddr, int len, union sp *buffer);
static void screen_reinit(unsigned cmask);
static void aicon_font_init(void);
static void aicon_size(Dimension *iw, Dimension *ih);
static void invert_icon(bool inverted);
static char *lff_single(const char *name, const char *reqd_display_charset,
    bool is_dbcs);
static char *load_fixed_font(const char *names, const char *reqd_charsets);
static void lock_icon(enum mcursor_state state);
static char *expand_cslist(const char *s);
static void hollow_cursor(int baddr);
static void revert_later(XtPointer closure _is_unused, XtIntervalId *id _is_unused);
static void xlate_dbcs(unsigned char, unsigned char, XChar2b *);
static void dfc_init(void);
static const char *dfc_search_family(const char *charset, dfc_t **dfc,
	void **cookie);
static bool dfc_search_name(const char *name);

static action_t SetFont_action;
static action_t Title_action;
static action_t WindowState_action;

/* Resize font list. */
struct rsfont {
	struct rsfont *next;
	char *name;
	int width;
	int height;
	int total_width;	/* transient */
	int total_height;	/* transient */
	int area;		/* transient */
};
static struct rsfont *rsfonts;

#define BASE_MASK		0x0f	/* mask for 16 actual colors */
#define INVERT_MASK		0x10	/* toggle for inverted colors */
#define GC_NONDEFAULT		0x20	/* distinguishes "color 0" from zeroed
					    memory */

#define COLOR_MASK		(GC_NONDEFAULT | BASE_MASK)
#define INVERT_COLOR(c)		((c) ^ INVERT_MASK)
#define NO_INVERT(c)		((c) & ~INVERT_MASK)

#define DEFAULT_PIXEL		(appres.m3279 ? HOST_COLOR_BLUE : FA_INT_NORM_NSEL)
#define PIXEL_INDEX(c)		((c) & BASE_MASK)


/*
 * Save 00 event translations.
 */
void
save_00translations(Widget w, XtTranslations *t00)
{
	*t00 = w->core.tm.translations;
}

/* 
 * Define our event translations
 */
void
set_translations(Widget w, XtTranslations *t00, XtTranslations *t0)
{
	struct trans_list *t;

	if (t00 != NULL)
		XtOverrideTranslations(w, *t00);

	for (t = trans_list; t != NULL; t = t->next)
		XtOverrideTranslations(w, lookup_tt(t->name, NULL));

	*t0 = w->core.tm.translations;
}

/*
 * Add or clear a temporary keymap.
 */
void
screen_set_temp_keymap(XtTranslations trans)
{
	if (trans != NULL) {
		XtOverrideTranslations(nss.widget, trans);
		XtOverrideTranslations(container, trans);
	} else {
		XtUninstallTranslations(nss.widget);
		XtOverrideTranslations(nss.widget, screen_t0);
		XtUninstallTranslations(container);
		XtOverrideTranslations(container, container_t0);
	}
}

/*
 * Change the baselevel keymap.
 */
void
screen_set_keymap(void)
{
	XtUninstallTranslations(nss.widget);
	set_translations(nss.widget, &screen_t00, &screen_t0);
	XtUninstallTranslations(container);
	set_translations(container, &container_t00, &container_t0);
}

/*
 * Screen pre-initialization (before charset init).
 */
void
screen_preinit(void)
{
    dfc_init();
}

/*
 * Initialize the screen.
 */
void
screen_init(void)
{
    int i;

    if (!appres.m3279) {
	appres.highlight_bold = true;
    }

    visible_control = toggled(VISIBLE_CONTROL);

    /* Parse the fixed window size, if there is any. */
    if (xappres.fixed_size) {
	char c;

	if (sscanf(xappres.fixed_size, "%ux%u%c", &fixed_width,
				&fixed_height, &c) != 2 ||
			!fixed_width ||
			!fixed_height) {
	    popup_an_error("Invalid fixed size");
	    fixed_width = 0;
	    fixed_height = 0;
	}
    }

    /* Initialize ss. */
    nss.cursor_daddr = 0;
    nss.exposed_yet = false;

    /* Initialize "gray" bitmap. */
    if (appres.interactive.mono) {
	gray = XCreatePixmapFromBitmapData(display,
		root_window, (char *)gray_bits,
		gray_width, gray_height,
		xappres.foreground, xappres.background, screen_depth);
    }

    /* Initialize the blank map. */
    (void) memset((char *)blank_map, '\0', sizeof(blank_map));
    for (i = 0; i < 256; i++) {
	if (ebc2asc0[i] == 0x20 || ebc2asc0[i] == 0xa0) {
	    BKM_SET(i);
	}
    }

    /* Initialize the emulated 3270 controller hardware. */
    ctlr_init(ALL_CHANGE);

    /* Initialize the actve icon. */
    aicon_init();

    /* Initialize the status line. */
    status_init();

    /* Initialize the placement of the pop-up keypad. */
    keypad_placement_init();

    /* Now call the "reinitialize" function to set everything else up. */
    screen_reinit(ALL_CHANGE);
}

/*
 * Re-initialize the screen.
 */
static void
screen_reinit(unsigned cmask)
{
	Dimension cwidth_curr;
	Dimension mkw;

	/* Allocate colors. */
	if (cmask & COLOR_CHANGE) {
	    if (appres.m3279) {
		default_color_scheme();
		(void) xfer_color_scheme(xappres.color_scheme, false);
	    }
	    allocate_pixels();
	}

	/* Define graphics contexts. */
	if (cmask & (FONT_CHANGE | COLOR_CHANGE))
		make_gcs(&nss);

	/* Reinitialize the controller. */
	ctlr_reinit(cmask);

	/* Allocate buffers. */
	if (cmask & MODEL_CHANGE) {
		/* Selection bitmap */
		Replace(selected,
		    (unsigned char *)XtCalloc(sizeof(unsigned char),
				              (maxROWS * maxCOLS + 7) / 8));

		/* X display image */
		Replace(nss.image, (union sp *)XtCalloc(sizeof(union sp),
							maxROWS * maxCOLS));
		Replace(temp_image, (union sp *)XtCalloc(sizeof(union sp),
							 maxROWS*maxCOLS));

		/* render_text buffers */
		Replace(rt_buf,
		    (XChar2b *)XtMalloc(maxCOLS * sizeof(XChar2b)));
	} else
		(void) memset((char *) nss.image, 0,
		              sizeof(union sp) * maxROWS * maxCOLS);

	/* Compute SBCS/DBCS size differences. */
	if ((cmask & FONT_CHANGE) && dbcs) {
		int wdiff, adiff, ddiff;
		char *xs;
		int xx;

#if defined(_ST) /*[*/
		printf("nss ascent %d descent %d\n"
		       "dbcs ascent %d descent %d\n",
		       nss.ascent, nss.descent,
		       dbcs_font.ascent, dbcs_font.descent);
#endif /*]*/

		/* Compute width difference. */
		wdiff = (2 * nss.char_width) - dbcs_font.char_width;
		if (wdiff > 0) {
			/* SBCS font is too wide */
			dbcs_font.xtra_width = wdiff;
#if defined(_ST) /*[*/
			printf("SBCS wider %d\n", wdiff);
#endif /*]*/
		} else if (wdiff < 0) {
			/* SBCS font is too narrow */
			if (wdiff % 2) {
				nss.xtra_width = (-wdiff)/2 + 1;
				dbcs_font.xtra_width = 1;
#if defined(_ST) /*[*/
				printf("SBCS odd\n");
#endif /*]*/
			} else
				nss.xtra_width = (-wdiff)/2;
#if defined(_ST) /*[*/
			printf("DBCS wider %d\n", -wdiff);
#endif /*]*/
		} else {
			dbcs_font.xtra_width = nss.xtra_width = 0;
#if defined(_ST) /*[*/
			printf("Width matches.\n");
#endif /*]*/
		}
		/* Add some extra on top of that. */
		if ((xs = getenv("X3270_XWIDTH")) != NULL) {
			xx = atoi(xs);
			if (xx && xx < 10) {
				nss.xtra_width += xx;
				dbcs_font.xtra_width += 2*xx;
			}
		}
		nss.char_width += nss.xtra_width;
		dbcs_font.char_width += dbcs_font.xtra_width;

		/*
		 * Compute height difference, doing ascent and descent
		 * separately.
		 */
		adiff = nss.ascent - dbcs_font.ascent;
		if (adiff > 0) {
#if defined(_ST) /*[*/
			printf("SBCS higher by %d\n", adiff);
			dbcs_font.ascent += adiff;
			dbcs_font.char_height += adiff;
#endif /*]*/
		} else if (adiff < 0) {
#if defined(_ST) /*[*/
			printf("DBCS higher by %d\n", -adiff);
			nss.ascent += -adiff;
			nss.char_height += -adiff;
#endif /*]*/
		} else {
#if defined(_ST) /*[*/
			printf("Ascent matches\n");
#endif /*]*/
		}
		ddiff = nss.descent - dbcs_font.descent;
		if (ddiff > 0) {
#if defined(_ST) /*[*/
			printf("SBCS lower by %d\n", ddiff);
#endif /*]*/
			dbcs_font.descent += ddiff;
			dbcs_font.char_height += ddiff;
		} else if (ddiff < 0) {
#if defined(_ST) /*[*/
			printf("DBCS lower by %d\n", -ddiff);
#endif /*]*/
			nss.descent += -ddiff;
			nss.char_height += -ddiff;
		} else {
#if defined(_ST) /*[*/
			printf("Descent matches\n");
#endif /*]*/
		}
		
		/* Add a constant to the height. */
		if ((xs = getenv("X3270_XHEIGHT")) != NULL) {
			xx = atoi(xs);
			if (xx && xx < 10) {
				dbcs_font.descent += xx;
				nss.descent += xx;
				nss.char_height += xx;
			}
		}
	}

	/* Set up a container for the menubar, screen and keypad */

	if (toggled(SCROLL_BAR))
		scrollbar_width = SCROLLBAR_WIDTH;
	else
		scrollbar_width = 0;

	if (1/*cmask & (FONT_CHANGE | MODEL_CHANGE)*/) {
		if (fixed_width) {
			Dimension w, h;

			/* Compute the halos. */
			hhalo = 0;
			w = SCREEN_WIDTH(ss->char_width) + scrollbar_width;
			if (w > fixed_width) {
				if (screen_redo == REDO_NONE)
					Error("Font is too wide for fixed "
							"width");
				hhalo = HHALO;
				(void) XtAppAddTimeOut(appcontext, 10,
						       revert_later, 0);
			} else
				hhalo = (fixed_width - w) / 2;
			vhalo = 0;
			h = SCREEN_HEIGHT(ss->char_height);
			if (h > fixed_height) {
				if (screen_redo == REDO_NONE)
					Error("Font is too tall for fixed "
							"width");
				vhalo = VHALO;
				(void) XtAppAddTimeOut(appcontext, 10,
						       revert_later, 0);
			} else
				vhalo = (fixed_height - h) / 2;
		}
		nss.screen_width  = SCREEN_WIDTH(ss->char_width);
		nss.screen_height = SCREEN_HEIGHT((ss->char_height));
	}

	if (fixed_width)
		container_width = fixed_width;
	else
		container_width = nss.screen_width+2 + scrollbar_width;
	cwidth_nkp = container_width;
	mkw = min_keypad_width();
	if (kp_placement == kp_integral && container_width < mkw) {
		keypad_xwidth = mkw - container_width;
		container_width = mkw;
	} else {
		keypad_xwidth = 0;
	}

	if (container == NULL) {
		container = XtVaCreateManagedWidget(
		    "container", huskWidgetClass, toplevel,
		    XtNborderWidth, 0,
		    XtNwidth, container_width,
		    XtNheight, 10,
			/* XXX -- a temporary lie to make Xt happy */
		    NULL);
		save_00translations(container, &container_t00);
		set_translations(container, NULL,
		    &container_t0);
		if (appres.interactive.mono)
			XtVaSetValues(container, XtNbackgroundPixmap, gray,
			    NULL);
		else
			XtVaSetValues(container, XtNbackground, keypadbg_pixel,
			    NULL);
	}

	/* Initialize the menu bar and integral keypad */

	cwidth_curr = xappres.keypad_on?
	    container_width: cwidth_nkp;
	menubar_height = menubar_qheight(cwidth_curr);
	menubar_init(container, container_width, cwidth_curr);

	if (fixed_height)
		container_height = fixed_height;
	else
		container_height = menubar_height + nss.screen_height+2;
	if (kp_placement == kp_integral) {
		(void) keypad_init(container, container_height,
		    container_width, false, false);
		keypad_height = keypad_qheight();
	} else
		keypad_height = 0;
	container_height += keypad_height;

	/* Create screen and set container dimensions */
	inflate_screen();

	/* Create scrollbar */
	scrollbar_init((cmask & MODEL_CHANGE) != 0);

	XtRealizeWidget(toplevel);
	nss.window = XtWindow(nss.widget);
	set_mcursor();

	/* Reinitialize the active icon. */
	aicon_reinit(cmask);

	/* Reinitialize the status line. */
	status_reinit(cmask);

	/* Initialize the input method. */
	if ((cmask & CHARSET_CHANGE) && dbcs) {
		xim_init();
	}

	cursor_changed = true;

	line_changed = true;

	/* Redraw the screen. */
	xaction_internal(PA_Expose_xaction, IA_REDRAW, NULL, NULL);
}


static void
set_toplevel_sizes(void)
{
    Dimension tw, th;

    tw = container_width - (xappres.keypad_on ? 0 : keypad_xwidth);
    th = container_height - (xappres.keypad_on ? 0 : keypad_height);
    if (fixed_width) {
	XtVaSetValues(toplevel,
		XtNwidth, fixed_width,
		XtNheight, fixed_height,
		NULL);
	XtVaSetValues(toplevel,
		XtNbaseWidth, fixed_width,
		XtNbaseHeight, fixed_height,
		XtNminWidth, fixed_width,
		XtNminHeight, fixed_height,
		XtNmaxWidth, fixed_width,
		XtNmaxHeight, fixed_height,
		NULL);
	XtVaSetValues(container,
		XtNwidth, fixed_width,
		XtNheight, fixed_height,
		NULL);
	main_width = fixed_width;
	main_height = fixed_height;
    } else {
	XtVaSetValues(toplevel,
		XtNwidth, tw,
		XtNheight, th,
		NULL);
	if (!allow_resize) {
	    XtVaSetValues(toplevel,
		    XtNbaseWidth, tw,
		    XtNbaseHeight, th,
		    XtNminWidth, tw,
		    XtNminHeight, th,
		    XtNmaxWidth, tw,
		    XtNmaxHeight, th,
		    NULL);
	}
	XtVaSetValues(container,
		XtNwidth, container_width,
		XtNheight, container_height,
		NULL);
	main_width = tw;
	main_height = th;
    }

    /*
     * Start a timer ticking, in case the window manager doesn't approve
     * of the change.
     */
    if (configure_ticking) {
	XtRemoveTimeOut(configure_id);
    }
    configure_id = XtAppAddTimeOut(appcontext, 500, configure_stable, 0);
    configure_ticking = true;

    keypad_move();
    {
	static bool first = true;
	if (first) {
	    first = false;
	} else {
	    popups_move();
	}
    }
}

static void
inflate_screen(void)
{
    /* Create the screen window */
    if (nss.widget == NULL) {
	nss.widget = XtVaCreateManagedWidget(
	    "screen", widgetClass, container,
	    XtNwidth, nss.screen_width,
	    XtNheight, nss.screen_height,
	    XtNx, xappres.keypad_on? (keypad_xwidth / 2): 0,
	    XtNy, menubar_height,
	    XtNbackground,
		appres.interactive.mono? xappres.background: colorbg_pixel,
	    NULL);
	save_00translations(nss.widget, &screen_t00);
	set_translations(nss.widget, NULL,
	    &screen_t0);
    } else {
	XtVaSetValues(nss.widget,
	    XtNwidth, nss.screen_width,
	    XtNheight, nss.screen_height,
	    XtNx, xappres.keypad_on? (keypad_xwidth / 2): 0,
	    XtNy, menubar_height,
	    XtNbackground,
		appres.interactive.mono? xappres.background: colorbg_pixel,
	    NULL);
    }

    /* Set the container and toplevel dimensions */
    XtVaSetValues(container,
	    XtNwidth, container_width,
	    XtNheight, container_height,
	    NULL);

    set_toplevel_sizes();
}

/* Scrollbar support. */
void
screen_set_thumb(float top, float shown)
{
	if (toggled(SCROLL_BAR))
		XawScrollbarSetThumb(scrollbar, top, shown);
}

static void
screen_scroll_proc(Widget w _is_unused, XtPointer client_data _is_unused,
    XtPointer position)
{
	scroll_proc((long)position, (int)nss.screen_height);
}

static void
screen_jump_proc(Widget w _is_unused, XtPointer client_data _is_unused,
    XtPointer percent_ptr)
{
	jump_proc(*(float *)percent_ptr);
}

/* Create, move, or reset the scrollbar. */
static void
scrollbar_init(bool is_reset)
{
    if (!scrollbar_width) {
	if (scrollbar != NULL) {
	    XtUnmapWidget(scrollbar);
	}
    } else {
	if (scrollbar == NULL) {
	    scrollbar = XtVaCreateManagedWidget(
		    "scrollbar", scrollbarWidgetClass,
		    container,
		    XtNx, nss.screen_width+1
			    + (xappres.keypad_on?
				(keypad_xwidth / 2): 0),
		    XtNy, menubar_height,
		    XtNwidth, scrollbar_width-1,
		    XtNheight, nss.screen_height,
		    XtNbackground, appres.interactive.mono?
			xappres.background: keypadbg_pixel,
		    NULL);
		XtAddCallback(scrollbar, XtNscrollProc,
		    screen_scroll_proc, NULL);
		XtAddCallback(scrollbar, XtNjumpProc,
		    screen_jump_proc, NULL);
	} else {
	    XtVaSetValues(scrollbar,
		    XtNx, nss.screen_width+1
			    + (xappres.keypad_on?
				(keypad_xwidth / 2) : 0),
		    XtNy, menubar_height,
		    XtNwidth, scrollbar_width-1,
		    XtNheight, nss.screen_height,
		    XtNbackground, appres.interactive.mono?
			xappres.background: keypadbg_pixel,
		    NULL);
		XtMapWidget(scrollbar);
	}
	XawScrollbarSetThumb(scrollbar, 0.0, 1.0);
    }

    /*
     * If the screen dimensions have changed, reallocate the scroll
     * save area.
     */
    if (is_reset || !scroll_initted) {
	scroll_buf_init();
    } else {
	rethumb();
    }
}

/* Turn the scrollbar on or off */
static void
toggle_scrollBar(toggle_index_t ix _is_unused, enum toggle_type tt _is_unused)
{
    scrollbar_changed = true;

    if (toggled(SCROLL_BAR)) {
	scrollbar_width = SCROLLBAR_WIDTH;
	screen_redo = REDO_SCROLLBAR;
    } else {
	scroll_to_bottom();
	scrollbar_width = 0;
    }

    screen_reinit(SCROLL_CHANGE);
    if (toggled(SCROLL_BAR)) {
	rethumb();
    }
}

/*
 * Called when a host connects, disconnects or changes NVT/3270 modes.
 */
static void
screen_connect(bool ignored _is_unused)
{
	if (ea_buf == NULL)
		return;		/* too soon */

	if (CONNECTED) {
		/*
		 * Clear the screen.
		 * If we're in NVT mode, go to the maximum screen dimensions,
		 * otherwise go to the default 24x80 for 3270 or SSCP mode.
		 */
		ctlr_erase((IN_NVT || IN_SSCP)? true: false);
		if (IN_3270)
			scroll_round();
		cursor_on();
		schedule_cursor_blink();
	} else {
		if (appres.disconnect_clear)
			ctlr_erase(true);
		(void) cursor_off();
	}
	if (toggled(CROSSHAIR)) {
		screen_changed = true;
		first_changed = 0;
		last_changed = ROWS*COLS;
		screen_disp(false);
	}

	mcursor_normal();
}

/*
 * Mouse cursor changes
 */

static void
set_mcursor(void)
{
	switch (mcursor_state) {
	    case LOCKED:
		XDefineCursor(display, nss.window, xappres.locked_mcursor);
		break;
	    case NORMAL:
		XDefineCursor(display, nss.window, xappres.normal_mcursor);
		break;
	    case WAIT:
		XDefineCursor(display, nss.window, xappres.wait_mcursor);
		break;
	}
	lock_icon(mcursor_state);
}

void
mcursor_normal(void)
{
	if (CONNECTED)
		mcursor_state = NORMAL;
	else if (HALF_CONNECTED)
		mcursor_state = WAIT;
	else
		mcursor_state = LOCKED;
	set_mcursor();
}

void
mcursor_waiting(void)
{
	mcursor_state = WAIT;
	set_mcursor();
}

void
mcursor_locked(void)
{
	mcursor_state = LOCKED;
	set_mcursor();
}

/*
 * Called from the keypad button to expose or hide the integral keypad.
 */
void
screen_showikeypad(bool on)
{
	if (on) {
		screen_redo = REDO_KEYPAD;
	}

	inflate_screen();
	if (keypad_xwidth > 0) {
		if (scrollbar != (Widget) NULL)
			scrollbar_init(false);
		menubar_resize(on ? container_width : cwidth_nkp);
	}
}


/*
 * The host just wrote a blinking character; make sure it blinks
 */
void
blink_start(void)
{
	text_blinkers_exist = true;
	if (!text_blink_scheduled) {
		/* Start in "on" state and start first iteration */
		text_blinking_on = true;
		schedule_text_blink();
	}
}

/*
 * Restore blanked blinking text
 */
static void
text_blink_it(XtPointer closure _is_unused, XtIntervalId *id _is_unused)
{
	/* Flip the state. */
	text_blinking_on = !text_blinking_on;

	/* Force a screen redraw. */
	ctlr_changed(0, ROWS*COLS);

	/* If there is still blinking text, schedule the next iteration */
	if (text_blinkers_exist)
		schedule_text_blink();
	else
		text_blink_scheduled = false;
}

/*
 * Schedule an event to restore blanked blinking text
 */
static void
schedule_text_blink(void)
{
	text_blink_scheduled = true;
	text_blink_id = XtAppAddTimeOut(appcontext, 500, text_blink_it, 0);
}


/*
 * Make the (displayed) cursor disappear.  Returns a bool indiciating if
 * the cursor was on before the call.
 */
static bool
cursor_off(void)
{
	if (cursor_displayed) {
		cursor_displayed = false;
		put_cursor(ss->cursor_daddr, false);
		return true;
	} else
		return false;
}



/*
 * Blink the cursor
 */
static void
cursor_blink_it(XtPointer closure _is_unused, XtIntervalId *id _is_unused)
{
	cursor_blink_pending = false;
	if (!CONNECTED || !toggled(CURSOR_BLINK))
		return;
	if (cursor_displayed) {
		if (in_focus)
			(void) cursor_off();
	} else
		cursor_on();
	schedule_cursor_blink();
}

/*
 * Schedule a cursor blink
 */
static void
schedule_cursor_blink(void)
{
	if (!toggled(CURSOR_BLINK) || cursor_blink_pending)
		return;
	cursor_blink_pending = true;
	cursor_blink_id = XtAppAddTimeOut(appcontext, 500, cursor_blink_it, 0);
}

/*
 * Cancel a cursor blink
 */
static void
cancel_blink(void)
{
	if (cursor_blink_pending) {
		XtRemoveTimeOut(cursor_blink_id);
		cursor_blink_pending = false;
	}
}

/*
 * Toggle cursor blinking (called from menu)
 */
static void
toggle_cursorBlink(toggle_index_t ix _is_unused, enum toggle_type tt _is_unused)
{
    if (!CONNECTED) {
	return;
    }

    if (toggled(CURSOR_BLINK)) {
	schedule_cursor_blink();
    } else {
	cursor_on();
    }
}

/*
 * Make the cursor visible at its (possibly new) location.
 */
static void
cursor_on(void)
{
	if (cursor_enabled && !cursor_displayed) {
		cursor_displayed = true;
		put_cursor(cursor_addr, true);
		ss->cursor_daddr = cursor_addr;
		cursor_changed = false;
	}
}


/*
 * Toggle the cursor (block/underline).
 */
static void
toggle_altCursor(toggle_index_t ix, enum toggle_type tt _is_unused)
{
    bool was_on;

    /* do_toggle already changed the value; temporarily change it back */
    toggle_toggle(ix);

    was_on = cursor_off();

    /* Now change it back again */
    toggle_toggle(ix);

    if (was_on) {
	cursor_on();
    }
}


/*
 * Move the cursor to the specified buffer address.
 */
void
cursor_move(int baddr)
{
	cursor_addr = baddr;
	cursor_pos();
}

/*
 * Display the cursor position on the status line
 */
static void
cursor_pos(void)
{
	if (!toggled(CURSOR_POS) || !CONNECTED)
		return;
	status_cursor_pos(cursor_addr);
}

/*
 * Toggle the display of the cursor position
 */
static void
toggle_cursorPos(toggle_index_t ix _is_unused, enum toggle_type tt _is_unused)
{
    if (toggled(CURSOR_POS)) {
	cursor_pos();
    } else {
	status_uncursor_pos();
    }
}

/*
 * Enable or disable cursor display (used by scroll logic)
 */
void
enable_cursor(bool on)
{
	if ((cursor_enabled = on) && CONNECTED) {
		cursor_on();
		cursor_changed = true;
	} else
		(void) cursor_off();
}


/*
 * Toggle the crosshair cursor.
 */
static void
toggle_crosshair(toggle_index_t ix _is_unused, enum toggle_type tt _is_unused)
{
    screen_changed = true;
    first_changed = 0;
    last_changed = ROWS*COLS;
    screen_disp(false);
}


/*
 * Toggle visible control characters.
 */
static void
toggle_visible_control(toggle_index_t ix _is_unused,
	enum toggle_type tt _is_unused)
{
    visible_control = toggled(VISIBLE_CONTROL);
    screen_changed = true;
    first_changed = 0;
    last_changed = ROWS*COLS;
    screen_disp(false);
}


/*
 * Redraw the screen.
 */
static void
do_redraw(Widget w, XEvent *event, String *params _is_unused,
	Cardinal *num_params _is_unused)
{
    int x, y, width, height;
    int startcol, ncols;
    int	startrow, endrow, row;
    int i;
    int c0;

    if (w == nss.widget) {
	keypad_first_up();
	if (xappres.active_icon && iconic) {
	    ss = &nss;
	    iconic = false;
	}
    } else if (xappres.active_icon && w == iss.widget) {
	if (xappres.active_icon && !iconic) {
	    ss = &iss;
	    iconic = true;
	}
    } else if (event) {
	return;
    }

    /* Only redraw as necessary for an expose event */
    if (event && event->type == Expose) {
	ss->exposed_yet = true;
	x = event->xexpose.x;
	y = event->xexpose.y;
	width = event->xexpose.width;
	height = event->xexpose.height;
	startrow = ssY_TO_ROW(y);
	if (startrow < 0) {
	    startrow = 0;
	}
	if (startrow > 0) {
	    startrow--;
	}
	endrow = ssY_TO_ROW(y+height);
	endrow = endrow >= maxROWS ? maxROWS : endrow + 1;
	startcol = ssX_TO_COL(x);
	if (startcol < 0) {
	    startcol = 0;
	}
	if (startcol > 0) {
	    startcol--;
	}
	if (startcol >= maxCOLS) {
	    goto no_draw;
	}
	ncols = (width / ss->char_width) + 2;
	if (startcol + ncols > maxCOLS) {
	    ncols = maxCOLS - startcol;
	}
	while ((ROWCOL_TO_BA(startrow, startcol) % maxCOLS) + ncols > maxCOLS) {
	    ncols--;
	}
	for (row = startrow; row < endrow; row++) {
	    (void) memset((char *) &ss->image[ROWCOL_TO_BA(row, startcol)],
		    0, ncols * sizeof(union sp));
	    if (visible_control) {
		c0 = ROWCOL_TO_BA(row, startcol);

		for (i = 0; i < ncols; i++) {
		    ss->image[c0 + i].bits.cc = EBC_space;
		}
	    }
	}
    no_draw:
	;

    } else {
	XFillRectangle(display, ss->window, get_gc(ss, INVERT_COLOR(0)), 0, 0,
		ss->screen_width, ss->screen_height);
	(void) memset((char *) ss->image, 0,
		(maxROWS*maxCOLS) * sizeof(union sp));
	if (visible_control) {
	    for (i = 0; i < maxROWS*maxCOLS; i++) {
		ss->image[i].bits.cc = EBC_space;
	    }
	}
	ss->copied = false;
    }
    ctlr_changed(0, ROWS*COLS);
    cursor_changed = true;
    if (!xappres.active_icon || !iconic) {
	line_changed = true;
	status_touch();
    }
}

/*
 * Explicitly redraw the screen (invoked from the keyboard).
 */
void
Redraw_xaction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    xaction_debug(Redraw_xaction, event, params, num_params);
    do_redraw(w, event, params, num_params);
}

/*
 * Implicitly redraw the screen (triggered by Expose events).
 */
void
PA_Expose_xaction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_Expose_xaction, event, params, num_params);
#endif /*]*/
    do_redraw(w, event, params, num_params);
}


/*
 * Redraw the changed parts of the screen.
 */

void
screen_disp(bool erasing)
{
    /* No point in doing anything if we aren't visible yet. */
    if (!ss->exposed_yet) {
	return;
    }

    /*
     * We don't set "cursor_changed" when the host moves the cursor,
     * 'cause he might just move it back later.  Set it here if the cursor
     * has moved since the last call to screen_disp.
     */
    if (cursor_addr != ss->cursor_daddr) {
	cursor_changed = true;
    }

    /* If the cursor has moved, tell the input method. */
    if (cursor_changed && ic != NULL &&
	    style == (XIMPreeditPosition|XIMStatusNothing)) {
#if defined(_ST) /*[*/
	printf("spot_loc%s\n", rcba(cursor_addr));
#endif /*]*/
	send_spot_loc();
    }

    /*
     * If only the cursor has changed (and not the screen image), draw it.
     */
    if (cursor_changed && !screen_changed) {
	if (cursor_off()) {
	    cursor_on();
	}
	if (toggled(CROSSHAIR)) {
	    screen_changed = true; /* repaint crosshair */
	}
    }

    /*
     * Redraw the parts of the screen that need refreshing, and redraw the
     * cursor if necessary.
     */
    if (screen_changed) {
	bool was_on = false;

	/* Draw the new screen image into "temp_image" */
	if (screen_changed) {
	    if (erasing) {
		crosshair_enabled = false;
	    }
	    draw_fields(temp_image, first_changed, last_changed);
	    if (erasing) {
		crosshair_enabled = true;
	    }
	}

	/* Set "cursor_changed" if the text under it has changed. */
	if (ss->image[fl_baddr(cursor_addr)].word !=
		temp_image[fl_baddr(cursor_addr)].word) {
	    cursor_changed = true;
	}

	/* Undraw the cursor, if necessary. */
	if (cursor_changed) {
	    was_on = cursor_off();
	}

	/* Intelligently update the X display with the new text. */
	resync_display(temp_image, first_changed, last_changed);

	/* Redraw the cursor. */
	if (was_on) {
	    cursor_on();
	}

	screen_changed = false;
	first_changed = -1;
	last_changed = -1;
    }

    if (!xappres.active_icon || !iconic) {
	/* Refresh the status line. */
	status_disp();

	/* Refresh the line across the bottom of the screen. */
	if (line_changed) {
	    XDrawLine(display, ss->window,
		    get_gc(ss, GC_NONDEFAULT | DEFAULT_PIXEL),
		    0,
		    ssROW_TO_Y(maxROWS-1)+SGAP-1,
		    ssCOL_TO_X(maxCOLS)+hhalo,
		    ssROW_TO_Y(maxROWS-1)+SGAP-1);
	    line_changed = false;
	}
    }
    draw_aicon_label();
}


/*
 * Render a blank rectangle on the X display.
 */
static void
render_blanks(int baddr, int height, union sp *buffer)
{
	int x, y;

#if defined(_ST) /*[*/
	(void) printf("render_blanks(baddr=%s, height=%d)\n", rcba(baddr),
	    height);
#endif /*]*/

	x = ssCOL_TO_X(BA_TO_COL(baddr));
	y = ssROW_TO_Y(BA_TO_ROW(baddr));

	XFillRectangle(display, ss->window,
	    get_gc(ss, INVERT_COLOR(0)),
	    x, y - ss->ascent,
	    (ss->char_width * COLS) + 1, (ss->char_height * height));

	(void) memmove(&ss->image[baddr], &buffer[baddr],
	                   COLS * height *sizeof(union sp));
}

/*
 * Check if a region of the screen is effectively empty: all blanks or nulls
 * with no extended highlighting attributes and not selected.
 *
 * Works _only_ with non-debug fonts.
 */
static bool
empty_space(register union sp *buffer, int len)
{
	register int i;

	for (i = 0; i < len; i++, buffer++) {
		if (buffer->bits.gr ||
		    buffer->bits.sel ||
		    (buffer->bits.fg & INVERT_MASK) ||
		    (buffer->bits.cs != CS_BASE) ||
		    !BKM_ISSET(buffer->bits.cc))
			return false;
	}
	return true;
}


/*
 * Reconcile the differences between a region of 'buffer' (what we want on
 * the X display) and ss->image[] (what is on the X display now).  The region
 * must not span lines.
 */
static void
resync_text(int baddr, int len, union sp *buffer)
{
	static bool ever = false;
	static unsigned long cmask = 0L;
	static unsigned long gmask = 0L;

#if defined(_ST) /*[*/
	(void) printf("resync_text(baddr=%s, len=%d)\n", rcba(baddr), len);
#endif /*]*/

	/*
	 * If the region begins on the right half of a DBCS character, back
	 * up one.
	 */
	if (baddr % COLS) {
		enum dbcs_state d;

		d = ctlr_dbcs_state(baddr);
		if (IS_RIGHT(d)) {
			baddr--;
			len++;
		}
	}

	if (!ever) {
		union sp b;

		/* Create masks for the "important" fields in an sp. */
		b.word = 0L;
		b.bits.fg = COLOR_MASK | INVERT_MASK;
		b.bits.sel = 1;
		b.bits.gr = GR_UNDERLINE | GR_INTENSIFY;
		cmask = b.word;

		b.word = 0L;
		b.bits.fg = INVERT_MASK;
		b.bits.sel = 1;
		b.bits.gr = 0xf;
		gmask = b.word;

		ever = true;
	}

	if (!visible_control &&
	    len > 1 &&
	    empty_space(&buffer[baddr], len)) {
		int x, y;

		x = ssCOL_TO_X(BA_TO_COL(baddr));
		y = ssROW_TO_Y(BA_TO_ROW(baddr));

		/* All empty, fill a rectangle */
#if defined(_ST) /*[*/
		(void) printf("FillRectangle(baddr=%s, len=%d)\n", rcba(baddr),
		    len);
#endif /*]*/
		XFillRectangle(display, ss->window,
		    get_gc(ss, INVERT_COLOR(0)),
		    x, y - ss->ascent,
		    (ss->char_width * len) + 1, ss->char_height);
	} else {
		unsigned long attrs, attrs2;
		bool has_gr, has_gr2;
		bool empty, empty2;
		union sp ra;
		int i;
		int i0 = 0;

		ra = buffer[baddr];

		/* Note the characteristics of the beginning of the region. */
		attrs = buffer[baddr].word & cmask;
		has_gr = (buffer[baddr].word & gmask) != 0;
		empty = !has_gr && BKM_ISSET(buffer[baddr].bits.cc);

		for (i = 0; i < len; i++) {
			/* Note the characteristics of this character. */
			attrs2 = buffer[baddr+i].word & cmask;
			has_gr2 = (buffer[baddr+i].word & gmask) != 0;
			empty2 = !has_gr2 && BKM_ISSET(buffer[baddr+i].bits.cc);

			/* If this character has exactly the same attributes
			   as the current region, simply add it, noting that
			   the region might now not be empty. */
			if (attrs2 == attrs) {
				if (!empty2)
					empty = 0;
				continue;
			}

			/* If this character is empty, and the current region
			   has no GR attributes, pretend it matches. */
			if (empty2 && !has_gr)
				continue;

			/* If the current region is empty, this character
			   isn't empty, and this character has no GR
			   attributes, change the current region's attributes
			   to this character's attributes and add it. */
			if (empty && !empty2 && !has_gr2) {
				attrs = attrs2;
				has_gr = has_gr2;
				empty = empty2;
				ra = buffer[baddr+i];
				continue;
			}

			/* Dump the region and start a new one with this
			   character. */
#if defined(_ST) /*[*/
			printf("%s:%d: rt%s\n", __FUNCTION__, __LINE__,
			    rcba(baddr+i0));
#endif /*]*/
			render_text(&buffer[baddr+i0], baddr+i0, i - i0, false,
			    &ra);
			attrs = attrs2;
			has_gr = has_gr2;
			empty = empty2;
			i0 = i;
			ra = buffer[baddr+i];
		}

		/* Dump the remainder of the region. */
#if defined(_ST) /*[*/
		printf("%s:%d: rt%s\n", __FUNCTION__, __LINE__, rcba(baddr+i0));
#endif /*]*/
		render_text(&buffer[baddr+i0], baddr+i0, len - i0, false, &ra);
	}

	/* The X display is now correct; update ss->image[]. */
	(void) memmove(&ss->image[baddr], &buffer[baddr], len*sizeof(union sp));
}

/*
 * Get a font index for an EBCDIC character.
 * Returns a blank if there is no mapping.
 *
 * Note that the EBCDIC character can be 16 bits (DBCS), and the output might
 * be 16 bits as well.
 */
static unsigned short
font_index(ebc_t ebc, int d8_ix, bool upper)
{
	ucs4_t ucs4;
	int d;

	ucs4 = ebcdic_base_to_unicode(ebc, EUO_BLANK_UNDEF | EUO_UPRIV);
	if (upper && ucs4 < 0x80 && islower(ucs4))
	    	ucs4 = toupper(ucs4);
	d = display8_lookup(d8_ix, ucs4);
	if (d < 0) {
	    	d = display8_lookup(d8_ix, ' ');
	}
	return d;
}

/*
 * Attempt to map an APL character to a DEC line-drawing character in the
 * first 32 bytes of an old 8-bit X11 font.
 */
static int
apl_to_linedraw(ebc_t c)
{
    	switch (c) {
	case 0xaf:	/* degree */
	    return 0x7;
	case 0xd4:	/* LR corner */
	    return 0xb;
	case 0xd5:	/* UR corner */
	    return 0xc;
	case 0xc5:	/* UL corner */
	    return 0xd;
	case 0xc4:	/* LL corner */
	    return 0xe;
	case 0xd3:	/* plus */
	    return 0xf;
	case 0xa2:	/* middle horizontal */
	    return 0x12;
	case 0xc6:	/* left tee */
	    return 0x15;
	case 0xd6:	/* right tee */
	    return 0x16;
	case 0xc7:	/* bottom tee */
	    return 0x17;
	case 0xd7:	/* top tee */
	    return 0x18;
	case 0xbf:	/* stile */
	case 0x85:	/* vertical line */
	    return 0x19;
	case 0x8c:	/* less or equal */
	    return 0x1a;
	case 0xae:	/* greater or equal */
	    return 0x1b;
	case 0xbe:	/* not equal */
	    return 0x1d;
	case 0xa3:	/* bullet */
	    return 0x1f;
	case 0xad:
	    return '[';
	case 0xbd:
	    return ']';
	default:
	    return -1;
	}
}

/* Map an APL character to the current display character set. */
static XChar2b
apl_to_udisplay(int d8_ix, unsigned char c)
{
	XChar2b x;
	int u = -1;
	int d = 0;

	/* Look it up. */
	u = apl_to_unicode(c, EUO_NONE);
	if (u != -1)
	    	d = display8_lookup(d8_ix, u);

	/* Default to a space. */
	if (d == 0)
		d = display8_lookup(d8_ix, ' ');

	/* Return it. */
	x.byte1 = (d >> 8) & 0xff;
	x.byte2 = d & 0xff;
	return x;
}

/* Map an APL character to the old first-32 8-bit X11 display character set. */
static XChar2b
apl_to_ldisplay(unsigned char c)
{
	XChar2b x;
	int u = -1;

	/* Look it up, defaulting to a space. */
	u = apl_to_linedraw(c);
	if (u == -1)
	    	u = ' ';

	/* Return it. */
	x.byte1 = 0;
	x.byte2 = u;
	return x;
}

/* Map a line-drawing character to the current display character set. */
static XChar2b
linedraw_to_udisplay(int d8_ix, unsigned char c)
{
	XChar2b x;
	int u = -1;
	int d = 0;

	/* Look it up. */
	u = linedraw_to_unicode(c);
	if (u != -1)
	    	d = display8_lookup(d8_ix, u);

	/* Default to a space. */
	if (d == 0)
		d = display8_lookup(d8_ix, ' ');

	/* Return it. */
	x.byte1 = (d >> 8) & 0xff;
	x.byte2 = d & 0xff;
	return x;
}

/*
 * Render text onto the X display.  The region must not span lines.
 */
static void
render_text(union sp *buffer, int baddr, int len, bool block_cursor,
    union sp *attrs)
{
	int color;
	int x, y;
	GC dgc = (GC)None;	/* drawing text */
	GC cleargc = (GC)None;	/* clearing under undersized characters */
	int sel = attrs->bits.sel;
	register int i, j;
	bool one_at_a_time = false;
	int d8_ix = ss->d8_ix;
	XTextItem16 text[64]; /* fixed size is a hack */
	int n_texts = -1;
	bool in_dbcs = false;
	int clear_len = 0;
	int n_sbcs = 0;
	int n_dbcs = 0;

#if defined(_ST) /*[*/
	(void) printf("render_text(baddr=%s, len=%d)\n", rcba(baddr), len);
#endif /*]*/

	/*
	 * If the region starts with the right-hand side of a DBCS, back off
	 * one column.
	 */
	switch (ctlr_dbcs_state(baddr)) {
	case DBCS_RIGHT:
	    /*
	     * Lots of assumptions -- the buffer really does go back one byte,
	     * and baddr is greater than zero.
	     */
#if defined(_ST) /*[*/
	    (void) printf("render_text: backing off\n");
#endif /*]*/
	    buffer--;
	    baddr--;
	    len++;
	    break;
	default:
	    break;
	}

	for (i = 0, j = 0; i < len; i++) {
		if (buffer[i].bits.cs != CS_DBCS || !dbcs || iconic) {
			if (n_texts < 0 || in_dbcs) {
				/* Switch from nothing or DBCS, to SBCS. */
#if defined(_ST) /*[*/
				fprintf(stderr, "SBCS starts at %s\n",
				    rcba(baddr + i));
#endif /*]*/
				in_dbcs = false;
				n_texts++;
				text[n_texts].chars = &rt_buf[j];
				text[n_texts].nchars = 0;
				text[n_texts].delta = 0;
				text[n_texts].font = ss->fid;
				n_sbcs++;
			}
			/* In SBCS. */
			clear_len += ss->char_width;
		} else {
			if (n_texts < 0 || !in_dbcs) {
				/* Switch from nothing or SBCS, to DBCS. */
#if defined(_ST) /*[*/
				fprintf(stderr, "DBCS starts at %s\n",
				    rcba(baddr + i));
#endif /*]*/
				in_dbcs = true;
				n_texts++;
				text[n_texts].chars = &rt_buf[j];
				text[n_texts].nchars = 0;
				text[n_texts].delta = 0;
				text[n_texts].font = dbcs_font.font;
				n_dbcs++;
			}
			/* In DBCS. */
			clear_len += 2 * ss->char_width;
		}

		switch (buffer[i].bits.cs) {
		    case CS_BASE:	/* latin-1 */
			rt_buf[j].byte1 = 0;
			if (toggled(MONOCASE))
				rt_buf[j].byte2 =
				    font_index(buffer[i].bits.cc, d8_ix, true);
			else {
				if (visible_control) {
					if (buffer[i].bits.cc == EBC_so) {
						rt_buf[j].byte1 = 0;
						rt_buf[j].byte2 =
						    font_index(EBC_less, d8_ix,
							    false);
					} else if (buffer[i].bits.cc == EBC_si) {
						rt_buf[j].byte1 = 0;
						rt_buf[j].byte2 =
						    font_index(EBC_greater,
							    d8_ix, false);
					} else {
					    	unsigned short c = font_index(
							buffer[i].bits.cc,
							d8_ix, false);

						rt_buf[j].byte1 =
						    (c >> 8) & 0xff;
						rt_buf[j].byte2 = c & 0xff;
					}
				} else {
				    	unsigned short c = font_index(
						buffer[i].bits.cc, d8_ix,
						false);

					rt_buf[j].byte1 =
					    (c >> 8) & 0xff;
					rt_buf[j].byte2 = c & 0xff;
				}
			}
			j++;
			break;
		    case CS_APL:	/* GE (apl) */
		    case CS_BASE | CS_GE:
			if (ss->extended_3270font) {
				rt_buf[j].byte1 = 1;
				rt_buf[j].byte2 = ebc2cg0[buffer[i].bits.cc];
			} else {
			    	if (ss->font_16bit) {
				    	rt_buf[j] = apl_to_udisplay(d8_ix,
						buffer[i].bits.cc);
				} else {
				    	rt_buf[j] = apl_to_ldisplay(
						buffer[i].bits.cc);
				}
			}
			j++;
			break;
		    case CS_LINEDRAW:	/* DEC line drawing */
			if (ss->standard_font) {
			    	if (ss->font_16bit) {
				    	rt_buf[j] = linedraw_to_udisplay(
						d8_ix, buffer[i].bits.cc);
				} else {
				    	/*
					 * Assume the first 32 characters are
					 * line-drawing.
					 */
					rt_buf[j].byte1 = 0;
					rt_buf[j].byte2 = buffer[i].bits.cc;
				}
			} else {
				if (ss->extended_3270font) {
					rt_buf[j].byte1 = 2;
					rt_buf[j].byte2 = buffer[i].bits.cc;
				} else {
					rt_buf[j].byte1 = 0;
					rt_buf[j].byte2 = 0;
				}
			}
			j++;
			break;
		    case CS_DBCS:	/* DBCS */
			if (dbcs) {
				xlate_dbcs(buffer[i].bits.cc,
					   buffer[i+1].bits.cc,
					   &rt_buf[j]);
				/* Skip the next byte as well. */
				i++;
			} else {
				rt_buf[j].byte1 = 0;
				rt_buf[j].byte2 = font_index(EBC_space, d8_ix,
					false);
			}
			j++;
			break;
		}
		text[n_texts].nchars++;
	}
	n_texts++;

	/* Check for one-at-a-time mode. */
	if (ss->funky_font) {
		for (i = 0; i < len; i++) {
			if (!rt_buf[i].byte1 &&
			    (IS_ODD(ss->odd_width, rt_buf[i].byte2) ||
			     IS_ODD(ss->odd_lbearing, rt_buf[i].byte2))) {
				one_at_a_time = true;
				break;
			}
		}
	}

	x = ssCOL_TO_X(BA_TO_COL(baddr));
	y = ssROW_TO_Y(BA_TO_ROW(baddr));
	color = attrs->bits.fg;

	/* Select the GCs. */
	if (sel && !block_cursor) {
		/* Selected, but not a block cursor. */
		if (!appres.interactive.mono) {
			/* Color: Use the special select GCs. */
			dgc = get_selgc(ss, color);
			cleargc = ss->clrselgc;
		} else {
			/* Mono: Invert the color. */
			dgc = get_gc(ss, INVERT_COLOR(color));
			cleargc = get_gc(ss, color);
		}
	} else if (block_cursor && !(appres.interactive.mono && sel)) {
		/* Block cursor, but neither mono nor selected. */
		if (xappres.use_cursor_color) {
			/* Use the specific-color inverted GC. */
			dgc = ss->invucgc;
			cleargc = ss->ucgc;
		} else {
			/* Just invert the specified color. */
			dgc = get_gc(ss, INVERT_COLOR(color));
			cleargc = get_gc(ss, color);
		}
	} else {
		/* Ordinary text, or a selected block cursor. */
		dgc = get_gc(ss, color);
		cleargc = get_gc(ss, INVERT_COLOR(color));
	}

	/* Draw the text */
	XFillRectangle(display, ss->window,
		cleargc,
		x, y - ss->ascent,
		clear_len, ss->char_height);
#if defined(_ST) /*[*/
	{
		int k, l;

		for (k = 0; k < n_texts; k++) {
			printf("text[%d]: %d chars, %s:", k,
			    text[k].nchars,
			    (text[k].font == dbcs_font.font)?
				"dbcs": "sbcs");
			for (l = 0; l < text[k].nchars; l++) {
			    printf(" %02x%02x", text[k].chars[l].byte1,
				text[k].chars[l].byte2);
			}
			printf("\n");
		}
	}
#endif /*]*/
	if (one_at_a_time ||
	    (n_sbcs && ss->xtra_width) || (n_dbcs && dbcs_font.xtra_width)) {
		int i, j;
		int xn = x;
		XTextItem16 text1;

		/* XXX: do overstrike */
		for (i = 0; i < n_texts; i++) {
			if (one_at_a_time || text[i].font == ss->fid) {
				if (one_at_a_time || ss->xtra_width) {
					for (j = 0; j < text[i].nchars;
					     j++) {
						text1.chars =
						    &text[i].chars[j];
						text1.nchars = 1;
						text1.delta = 0;
						text1.font = ss->fid;
						XDrawText16(
						    display,
						    ss->window, dgc,
						    xn, y, &text1,
						    1);
						xn += ss->char_width;
					}
				} else {
					XDrawText16(display,
					    ss->window, dgc, xn, y,
					    &text[i], 1);
					xn += ss->char_width *
						text[i].nchars;
				}
			} else {
				if (dbcs_font.xtra_width) {
					for (j = 0; j < text[i].nchars;
					     j++) {
						text1.chars =
						    &text[i].chars[j];
						text1.nchars = 1;
						text1.delta = 0;
						text1.font =
						    dbcs_font.font;
						XDrawText16(
						    display,
						    ss->window, dgc,
						    xn,
						    y,
						    &text1,
						    1);
						xn += dbcs_font.char_width;
					}
				} else {
					XDrawText16(display,
					    ss->window, dgc, xn,
					    y,
					    &text[i], 1);
					xn += dbcs_font.char_width *
						text[i].nchars;
				}
			}
		}
	} else {
		XDrawText16(display, ss->window, dgc, x, y, text, n_texts);
		if (ss->overstrike &&
		    ((attrs->bits.gr & GR_INTENSIFY) ||
		     ((appres.interactive.mono || (!appres.m3279 && appres.highlight_bold)) &&
		      ((color & BASE_MASK) == FA_INT_HIGH_SEL)))) {
			XDrawText16(display, ss->window, dgc, x+1, y,
			    text, n_texts);
		}
	}

	if (attrs->bits.gr & GR_UNDERLINE)
		XDrawLine(display,
		    ss->window,
		    dgc,
		    x,
		    y - ss->ascent + ss->char_height - 1,
		    x + clear_len,
		    y - ss->ascent + ss->char_height - 1);
}


bool
screen_obscured(void)
{
	return ss->obscured;
}

/*
 * Scroll the screen image one row.
 *
 * This is the optimized path from ctlr_scroll(); it assumes that ea_buf[] has
 * already been modified and that the screen can be brought into sync by
 * hammering ss->image and the bitmap.
 */
void
screen_scroll(void)
{
	bool was_on;

	if (!ss->exposed_yet)
		return;

	was_on = cursor_off();
	(void) memmove(&ss->image[0], &ss->image[COLS],
	                   (ROWS - 1) * COLS * sizeof(union sp));
	(void) memmove(&temp_image[0], &temp_image[COLS],
	                   (ROWS - 1) * COLS * sizeof(union sp));
	(void) memset((char *)&ss->image[(ROWS - 1) * COLS], 0,
	              COLS * sizeof(union sp));
	(void) memset((char *)&temp_image[(ROWS - 1) * COLS], 0,
	              COLS * sizeof(union sp));
	XCopyArea(display, ss->window, ss->window, get_gc(ss, 0),
	    ssCOL_TO_X(0),
	    ssROW_TO_Y(1) - ss->ascent,
	    ss->char_width * COLS,
	    ss->char_height * (ROWS - 1),
	    ssCOL_TO_X(0),
	    ssROW_TO_Y(0) - ss->ascent);
	ss->copied = true;
	XFillRectangle(display, ss->window, get_gc(ss, INVERT_COLOR(0)),
	    ssCOL_TO_X(0),
	    ssROW_TO_Y(ROWS - 1) - ss->ascent,
	    (ss->char_width * COLS) + 1,
	    ss->char_height);
	if (was_on)
		cursor_on();
}


/*
 * Toggle mono-/dual-case mode.
 */
static void
toggle_monocase(toggle_index_t ix _is_unused, enum toggle_type tt _is_unused)
{
    (void) memset((char *)ss->image, 0, (ROWS*COLS) * sizeof(union sp));
    ctlr_changed(0, ROWS*COLS);
}

/**
 * Toggle timing display.
 */
static void
toggle_showTiming(toggle_index_t ix _is_unused, enum toggle_type tt _is_unused)
{
    if (!toggled(SHOW_TIMING)) {
	status_untiming();
    }
}

/*
 * Toggle screen flip
 */
void
screen_flip(void)
{
    /* Flip mode is broken in the DBCS version. */
    if (!dbcs) {
	flipped = !flipped;

	xaction_internal(PA_Expose_xaction, IA_REDRAW, NULL, NULL);
    }
}

/*
 * Return a visible control character for a field attribute.
 */
static unsigned char
visible_ebcdic(unsigned char fa)
{
    static unsigned char varr[32] = {
	EBC_0, EBC_1, EBC_2, EBC_3, EBC_4, EBC_5, EBC_6, EBC_7,
	EBC_8, EBC_9, EBC_A, EBC_B, EBC_C, EBC_D, EBC_E, EBC_F,
	EBC_G, EBC_H, EBC_I, EBC_J, EBC_K, EBC_L, EBC_M, EBC_N,
	EBC_O, EBC_P, EBC_Q, EBC_R, EBC_S, EBC_T, EBC_U, EBC_V
    };

    unsigned ix;

    /*
     * This code knows that:
     *  FA_PROTECT is   0b100000, and we map it to 0b010000
     *  FA_NUMERIC is   0b010000, and we map it to 0b001000
     *  FA_INTENSITY is 0b001100, and we map it to 0b000110
     *  FA_MODIFY is    0b000001, and we copy to   0b000001
     */
    ix = ((fa & (FA_PROTECT | FA_NUMERIC | FA_INTENSITY)) >> 1) |
	(fa & FA_MODIFY);
    return varr[ix];
}

/*
 * "Draw" ea_buf into a buffer
 */
static void
draw_fields(union sp *buffer, int first, int last)
{
	int	baddr = 0;
	int	faddr;
	unsigned char	fa;
	struct ea       *field_ea;
	struct ea	*sbp = ea_buf;
	int	field_color;
	int	zero;
	bool	any_blink = false;
	int	crossable = CROSSABLE;
	enum dbcs_state d;
	int	cursor_col = 0, cursor_row = 0;

	/* Set up cursor_col/cursor_row. */
	if (crossable) {
		cursor_col = BA_TO_COL(cursor_addr);
		cursor_row = BA_TO_ROW(cursor_addr);
	}

	/* If there is any blinking text, override the suggested boundaries. */
	if (text_blinkers_exist) {
		first = -1;
		last = -1;
	}

	/* Adjust pointers to start of region. */
	if (first > 0) {
		baddr += first;
		sbp += first;
		buffer += first;
	}
	faddr = find_field_attribute(baddr);
	fa = ea_buf[faddr].fa;
	field_ea = fa2ea(faddr);

	/* Adjust end of region. */
	if (last == -1 || last >= ROWS*COLS)
		last = 0;

	zero = FA_IS_ZERO(fa);
	if (field_ea->fg && (!appres.modified_sel || !FA_IS_MODIFIED(fa)))
		field_color = field_ea->fg & COLOR_MASK;
	else
		field_color = fa_color(fa);

	do {
		unsigned char	c = sbp->cc;
		union sp	b;
		bool		reverse = false;
		bool		is_selected = false;
		bool		is_crossed = false;

		b.word = 0;	/* clear out all fields */

		if (ea_buf[baddr].fa) {
		    fa = ea_buf[baddr].fa;
		    field_ea = sbp;
			zero = FA_IS_ZERO(fa);
			if (field_ea->fg && (!appres.modified_sel || !FA_IS_MODIFIED(fa)))
				field_color = field_ea->fg & COLOR_MASK;
			else
				field_color = fa_color(fa);
			if (visible_control) {
				b.bits.cc = visible_ebcdic(fa);
				b.bits.gr = GR_UNDERLINE;
				b.bits.fg = GC_NONDEFAULT | HOST_COLOR_YELLOW;
			}
		} else {
			unsigned short gr;
			int e_color;
			bool is_vc = false;

			/* Find the right graphic rendition. */
			if (zero) {
			    	gr = 0;
			} else {
				gr = sbp->gr;
				if (!gr)
					gr = field_ea->gr;
				if (gr & GR_BLINK)
					any_blink = true;
				if (appres.highlight_bold && FA_IS_HIGH(fa))
					gr |= GR_INTENSIFY;
			}

			/* Find the right color. */
			if (zero) {
			    	e_color = fa_color(FA_INT_HIGH_SEL);
			} else {
				if (sbp->fg)
					e_color = sbp->fg & COLOR_MASK;
				else if (appres.interactive.mono &&
					(gr & GR_INTENSIFY)) {
					e_color = fa_color(FA_INT_HIGH_SEL);
				} else {
					e_color = field_color;
				}
				if (gr & GR_REVERSE) {
					e_color = INVERT_COLOR(e_color);
					reverse = true;
				}
			}
			if (!appres.interactive.mono)
				b.bits.fg = e_color;

			/* Find the right character and character set. */
			d = ctlr_dbcs_state(baddr);
			if (zero) {
				if (visible_control)
					b.bits.cc = EBC_space;
			} else if (((!visible_control || c != EBC_null) &&
				    (c != EBC_space || d != DBCS_NONE)) ||
			           (gr & (GR_REVERSE | GR_UNDERLINE)) ||
				   visible_control) {

				b.bits.fg = e_color;

				/*
				 * Replace blanked-out blinking text with
				 * spaces.
				 */
				if (!text_blinking_on && (gr & GR_BLINK))
					b.bits.cc = EBC_space;
				else {
					if (visible_control && c == EBC_null) {
						b.bits.cc = EBC_period;
						is_vc = true;
					} else if (visible_control &&
					    (c == EBC_so || c == EBC_si)) {
						b.bits.cc = (c == EBC_so)?
						    EBC_less:
						    EBC_greater;
						is_vc = true;
					} else
						b.bits.cc = c;
					if (sbp->cs)
						b.bits.cs = sbp->cs;
					else
						b.bits.cs = field_ea->cs;
					if (b.bits.cs & CS_GE)
						b.bits.cs = CS_APL;
					else if ((b.bits.cs & CS_MASK) !=
						    CS_DBCS ||
						 d != DBCS_NONE)
						b.bits.cs &= CS_MASK;
					else
						b.bits.cs = CS_BASE;
				}

			} /* otherwise, EBC_null */

			if (visible_control) {
				if (is_vc)
					b.bits.gr = GR_UNDERLINE;
			} else {
				b.bits.gr = gr & (GR_UNDERLINE | GR_INTENSIFY);
			}

			/* Check for SI/SO. */
			if (d == DBCS_LEFT || d == DBCS_RIGHT)
				b.bits.cs = CS_DBCS;
		}

		/*
		 * Compute selection state.
		 *
		 * DBCS characters always act as a unit, with the state
		 * determined by the selection status and crosshair
		 * intersection of either half.
		 * - If either half is selected, both are considered selected.
		 * - If either half lies in the crosshair, neither is
		 *   considered selected.
		 */

		is_selected = (SELECTED(baddr) != 0);
		switch (ctlr_dbcs_state(baddr)) {
		case DBCS_NONE:
		case DBCS_DEAD:
		case DBCS_LEFT_WRAP:
		case DBCS_RIGHT_WRAP:
		    break;
		case DBCS_LEFT:
		case DBCS_SI:
		    if ((baddr % COLS) != (COLS - 1) &&
			SELECTED(baddr + 1))
			    is_selected = true;
		    break;
		case DBCS_RIGHT:
		case DBCS_SB: /* XXX */
		    if ((baddr % COLS) && SELECTED(baddr - 1))
			    is_selected = true;
		    break;
		}

		if (crossable && !reverse) {
			is_crossed = CROSSED(baddr);
			switch (ctlr_dbcs_state(baddr)) {
			case DBCS_NONE:
			case DBCS_DEAD:
			case DBCS_LEFT_WRAP:
			case DBCS_RIGHT_WRAP:
			    break;
			case DBCS_LEFT:
			case DBCS_SI:
			    if ((baddr % COLS) != (COLS - 1) &&
				CROSSED(baddr + 1))
				    is_crossed = true;
			    break;
			case DBCS_RIGHT:
			case DBCS_SB: /* XXX */
			    if ((baddr % COLS) && CROSSED(baddr - 1))
				    is_crossed = true;
			    break;
			}
		}

		/*
		 * XOR the crosshair cursor with selections.
		 */
		if (crossable) {
			if (is_selected ^ is_crossed)
				b.bits.sel = 1;
		} else {
			if (is_selected)
				b.bits.sel = 1;
		}

		if (!flipped)
			*buffer++ = b;
		else
			*(buffer + fl_baddr(baddr)) = b;
		sbp++;
		INC_BA(baddr);
	} while (baddr != last);

	/* Cancel blink timeouts if none were seen this pass. */
	if (!any_blink)
		text_blinkers_exist = false;
}


/*
 * Resync the X display with the contents of 'buffer'
 */
static void
resync_display(union sp *buffer, int first, int last)
{
	register int	i, j;
	int		b = 0;
	int		i0 = -1;
	bool		ccheck;
	int		fca = fl_baddr(cursor_addr);
	int		first_row, last_row;
#	define SPREAD	10

	if (first < 0) {
		first_row = 0;
		last_row = ROWS;
	} else {
		first_row = first / COLS;
		b = first_row * COLS;
		last_row = (last + (COLS-1)) / COLS;
	}

	for (i = first_row; i < last_row; b += COLS, i++) {
		int d0 = -1;
		int s0 = -1;

		/* Has the line changed? */
		if (!memcmp((char *) &ss->image[b], (char *) &buffer[b],
		    COLS*sizeof(union sp))) {
			if (i0 >= 0) {
				render_blanks(i0 * COLS, i - i0, buffer);
				i0 = -1;
			}
			continue;
		}

		/* Is the new value empty? */
		if (!visible_control &&
		    !(fca >= b && fca < (b+COLS)) &&
		    empty_space(&buffer[b], COLS)) {
			if (i0 < 0)
				i0 = i;
			continue;
		}

		/* Yes, it changed, and it isn't blank.
		   Dump any pending blank lines. */
		if (i0 >= 0) {
			render_blanks(i0 * COLS, i - i0, buffer);
			i0 = -1;
		}

		/* New text.  Scan it. */
		ccheck = cursor_displayed &&
			 fca >= b &&
			 fca < (b+COLS);
		for (j = 0; j < COLS; j++) {
			if (ccheck && b+j == fca) {
				/* Don't repaint over the cursor. */

				/* Dump any pending "different" characters. */
				if (d0 >= 0)
					resync_text(b+d0, j-d0, buffer);

				/* Start over. */
				d0 = -1;
				s0 = -1;
				continue;
			}
			if (ss->image[b+j].word == buffer[b+j].word) {

				/* Character is the same. */

				if (d0 >= 0) {
					/* Something is pending... */
					if (s0 < 0) {
						/* Start of "same" area */
						s0 = j;
					} else {
						/* nth matching character */
						if (j - s0 > SPREAD) {
							/* too many */
							resync_text(b+d0,
							    s0-d0, buffer);
							d0 = -1;
							s0 = -1;
						}
					}
				}
			} else {

				/* Character is different. */

				/* Forget intermediate matches. */
				s0 = -1;

				if (d0 < 0)
					/* Mark the start. */
					d0 = j;
			}
		}

		/* Dump any pending "different" characters. */
		if (d0 >= 0)
			resync_text(b+d0, COLS-d0, buffer);
	}
	if (i0 >= 0)
		render_blanks(i0 * COLS, last_row - i0, buffer);
}


/*
 * Support code for cursor redraw.
 */

/*
 * Calculate a flipped baddr.
 */
static int
fl_baddr(int baddr)
{
	if (!flipped)
		return baddr;
	return ((baddr / COLS) * COLS) + (COLS - (baddr % COLS) - 1);
}

/*
 * Return the proper foreground color for a character position.
 */

static int
char_color(int baddr)
{
	int faddr;
	unsigned char fa;
	int color;

	faddr = find_field_attribute(baddr);
	fa = ea_buf[faddr].fa;

	/*
	 * For non-display fields, we ignore gr and fg.
	 */
	if (FA_IS_ZERO(fa)) {
		color = fa_color(fa);
		if (appres.interactive.mono && SELECTED(baddr)) {
			color = INVERT_COLOR(color);
		}
		return color;
	}

	/*
	 * Find the color of the character or the field.
	 */
	if (ea_buf[baddr].fg)
		color = ea_buf[baddr].fg & COLOR_MASK;
	else if (fa2ea(faddr)->fg && (!appres.modified_sel ||
				      !FA_IS_MODIFIED(fa)))
		color = fa2ea(faddr)->fg & COLOR_MASK;
	else
		color = fa_color(fa);

	/*
	 * Now apply reverse video.
	 *
	 * One bit of strangeness:
	 *  If the buffer is a field attribute and we aren't using the
	 *  debug font, it's displayed as a blank; don't invert.
	 */
	if (!((ea_buf[baddr].fa && !visible_control)) &&
	    ((ea_buf[baddr].gr & GR_REVERSE) ||
	     (fa2ea(faddr)->gr & GR_REVERSE)))
		color = INVERT_COLOR(color);

	/*
	 * In monochrome, apply selection status as well.
	 */
	if (appres.interactive.mono && SELECTED(baddr))
		color = INVERT_COLOR(color);

	return color;
}


/*
 * Select a GC for drawing a hollow or underscore cursor.
 */
static GC
cursor_gc(int baddr)
{
    /*
     * If they say use a particular color, use it.
     */
    if (xappres.use_cursor_color) {
	return ss->ucgc;
    } else {
	return get_gc(ss, char_color(baddr));
    }
}

/*
 * Redraw one character.
 * If 'invert' is true, invert the foreground and background colors.
 */
static void
redraw_char(int baddr, bool invert)
{
	enum dbcs_state d;
	union sp buffer[2];
	int faddr;
	unsigned char fa;
	int gr;
	int blank_it = 0;
	int baddr2;
	int len = 1;

	/*
	 * Figure out the DBCS state of this position.  If it's the right-hand
	 * side of a DBCS character, repaint the left side instead.
	 */
	switch ((d = ctlr_dbcs_state(baddr))) {
	case DBCS_LEFT:
	case DBCS_SI:
	    len = 2;
	    break;
	case DBCS_RIGHT:
	    len = 2;
	    DEC_BA(baddr);
	    break;
	default:
	    break;
	}

	if (!invert) {
		int flb = fl_baddr(baddr);

		/*
		 * Put back what belongs there.
		 * Note that the cursor may have been covering a DBCS character
		 * that is no longer DBCS, so if we're not at the right margin,
		 * we should redraw two positions.
		 */
#if defined(_ST) /*[*/
		printf("%s:%d: rt%s\n", __FUNCTION__, __LINE__, rcba(flb));
#endif /*]*/
		if (dbcs && ((baddr % COLS) != (COLS - 1)) && len == 1)
			len = 2;
		render_text(&ss->image[flb], flb, len, false, &ss->image[flb]);
		return;
	}

	baddr2 = baddr;
	INC_BA(baddr2);

	/*
	 * Fabricate the right thing.
	 * ss->image isn't going to help, because it may contain shortcuts
	 *  for faster display, so we have to construct a buffer to use.
	 */
	buffer[0].word = 0L;
	buffer[0].bits.cc = ea_buf[baddr].cc;
	buffer[0].bits.cs = ea_buf[baddr].cs;
	if (buffer[0].bits.cs & CS_GE)
		buffer[0].bits.cs = CS_APL;
	else
		buffer[0].bits.cs &= CS_MASK;

	faddr = find_field_attribute(baddr);
	if (d == DBCS_LEFT || d == DBCS_RIGHT)
		buffer[0].bits.cs = CS_DBCS;
	fa = ea_buf[faddr].fa;
	if (FA_IS_ZERO(fa)) {
		gr = 0;
	} else {
		gr = ea_buf[baddr].gr;
		if (!gr)
			gr = fa2ea(faddr)->gr;
	}
	if (ea_buf[baddr].fa) {
		if (!visible_control)
			blank_it = 1;
	} else if (FA_IS_ZERO(fa)) {
		blank_it = 1;
	} else if (text_blinkers_exist && !text_blinking_on) {
		if (gr & GR_BLINK)
			blank_it = 1;
	}
	if (blank_it) {
		buffer[0].bits.cc = EBC_space;
		buffer[0].bits.cs = 0;
	}
	buffer[0].bits.fg = char_color(baddr);
	buffer[0].bits.gr |= (gr & GR_INTENSIFY);
	if (len == 2) {
		buffer[1].word = buffer[0].word;
		if (!blank_it)
			buffer[1].bits.cc = ea_buf[baddr2].cc;
	}
	render_text(buffer, fl_baddr(baddr), len, true, buffer);

}

/*
 * Draw a hollow cursor.
 */
static void
hollow_cursor(int baddr)
{
	Dimension cwidth;
	enum dbcs_state d;

	d = ctlr_dbcs_state(baddr);

	switch (d) {
	case DBCS_RIGHT:
	    DEC_BA(baddr);
	    /* fall through... */
	case DBCS_LEFT:
	case DBCS_SI:
	    cwidth = (2 * ss->char_width) - 1;
	    break;
	default:
	    cwidth = ss->char_width - 1;
	    break;
	}

	XDrawRectangle(display,
	    ss->window,
	    cursor_gc(baddr),
	    ssCOL_TO_X(BA_TO_COL(fl_baddr(baddr))),
	    ssROW_TO_Y(BA_TO_ROW(baddr)) - ss->ascent +
		(appres.interactive.mono ? 1 : 0),
	    cwidth,
	    ss->char_height - (appres.interactive.mono ? 2 : 1));
}

/*
 * Draw an underscore cursor.
 */
static void
underscore_cursor(int baddr)
{
	Dimension cwidth;
	enum dbcs_state d;

	d = ctlr_dbcs_state(baddr);

	switch (d) {
	case DBCS_RIGHT:
	    DEC_BA(baddr);
	    /* fall through... */
	case DBCS_LEFT:
	case DBCS_SI:
	    cwidth = (2 * ss->char_width) - 1;
	    break;
	default:
	    cwidth = ss->char_width - 1;
	    break;
	}

	XDrawRectangle(display,
	    ss->window,
	    cursor_gc(baddr),
	    ssCOL_TO_X(BA_TO_COL(fl_baddr(baddr))),
	    ssROW_TO_Y(BA_TO_ROW(baddr)) - ss->ascent +
		ss->char_height - 2,
	    cwidth,
	    1);
}

/*
 * Invert a square over a character.
 */
static void
small_inv_cursor(int baddr)
{
	/* XXX: DBCS? */

	XFillRectangle(display,
	    ss->window,
	    ss->mcgc,
	    ssCOL_TO_X(BA_TO_COL(fl_baddr(baddr))),
	    ssROW_TO_Y(BA_TO_ROW(baddr)) - ss->ascent + 1,
	    ss->char_width,
	    (ss->char_height > 2) ? (ss->char_height - 2) : 1);
}

/*
 * Draw or remove the cursor.
 */
static void
put_cursor(int baddr, bool on)
{
	/*
	 * If the cursor is being turned off, simply redraw the text under it.
	 */
	if (!on) {
		redraw_char(baddr, false);
		return;
	}

	/*
	 * If underscore cursor, redraw the character and draw the underscore.
	 */
	if (toggled(ALT_CURSOR)) {
		redraw_char(baddr, false);
		underscore_cursor(baddr);
		return;
	}

	/*
	 * On, and not an underscore.
	 *
	 * If out of focus, either draw an empty box in its place (if block
	 * cursor) or redraw the underscore (if underscore).
	 */
	if (!in_focus) {
		hollow_cursor(baddr);
		return;
	}

	/*
	 * If monochrome, invert a small square over the characters.
	 */
	if (appres.interactive.mono) {
		small_inv_cursor(baddr);
		return;
	}

	/*
	 * Color: redraw the character in reverse video.
	 */
	redraw_char(baddr, true);
}


/* Allocate a named color. */
static bool
alloc_color(char *name, enum fallback_color fb_color, Pixel *pixel)
{
	XColor cell, db;
	Screen *s;

	s = XtScreen(toplevel);

	if (name[0] == '#') {
		unsigned long rgb;
		char *endptr;

		rgb = strtoul(name + 1, &endptr, 16);
		if (endptr != name + 1 && !*endptr && !(rgb & ~0xffffff)) {
			(void) memset(&db, '\0', sizeof(db));
			db.red = (rgb >> 16) & 0xff;
			db.red |= (db.red << 8);
			db.green = (rgb >> 8) & 0xff;
			db.green |= (db.green << 8);
			db.blue = rgb & 0xff;
			db.blue |= (db.blue << 8);
			if (XAllocColor(display, XDefaultColormapOfScreen(s),
						&db) != 0) {
				*pixel = db.pixel;
				return true;
			}
		}
	} else {
		if (XAllocNamedColor(display, XDefaultColormapOfScreen(s), name,
		    &cell, &db) != 0) {
			*pixel = db.pixel;
			return true;
		}
	}
	switch (fb_color) {
	    case FB_WHITE:
		*pixel = XWhitePixelOfScreen(s);
		break;
	    case FB_BLACK:
		*pixel = XBlackPixelOfScreen(s);
		break;
	}
	return false;
}

/* Spell out a fallback color. */
static const char *
fb_name(enum fallback_color fb_color)
{
	switch (fb_color) {
	    case FB_WHITE:
		return "white";
	    case FB_BLACK:
		return "black";
	}
	return "chartreuse";	/* to keep Gcc -Wall happy */
}

/* Allocate color pixels. */
static void
allocate_pixels(void)
{

    if (appres.interactive.mono) {
	return;
    }

    /* Allocate constant elements. */
    if (!alloc_color(xappres.colorbg_name, FB_BLACK, &colorbg_pixel)) {
	popup_an_error("Cannot allocate colormap \"%s\" for screen "
		"background, using \"black\"", xappres.colorbg_name);
    }
    if (!alloc_color(xappres.selbg_name, FB_BLACK, &selbg_pixel)) {
	popup_an_error("Cannot allocate colormap \"%s\" for select "
		"background, using \"black\"", xappres.selbg_name);
    }
    if (!alloc_color(xappres.keypadbg_name, FB_WHITE, &keypadbg_pixel)) {
	popup_an_error("Cannot allocate colormap \"%s\" for keypad "
		"background, using \"white\"", xappres.keypadbg_name);
    }
    if (xappres.use_cursor_color &&
	    !alloc_color(xappres.cursor_color_name, FB_WHITE,
		&cursor_pixel)) {
	popup_an_error("Cannot allocate colormap \"%s\" for cursor color, "
		"using \"white\"", xappres.cursor_color_name);
    }

    /* Allocate pseudocolors. */
    if (!appres.m3279) {
	if (!alloc_color(xappres.normal_name, FB_WHITE, &normal_pixel)) {
	    popup_an_error("Cannot allocate colormap \"%s\" for text, "
		    "using \"white\"", xappres.normal_name);
	}
	if (!alloc_color(xappres.select_name, FB_WHITE, &select_pixel)) {
	    popup_an_error("Cannot allocate colormap \"%s\" for selectable "
		    "text, using \"white\"", xappres.select_name);
	}
	if (!alloc_color(xappres.bold_name, FB_WHITE, &bold_pixel)) {
	    popup_an_error("Cannot allocate colormap \"%s\" for bold text, "
		    "using \"white\"", xappres.bold_name);
	}
    }
}

/* Deallocate pixels. */
static void
destroy_pixels(void)
{
	int i;

	/*
	 * It would make sense to deallocate many of the pixels here, but
	 * the only available call (XFreeColors) would deallocate cells
	 * that may be in use by other Xt widgets.  Occh.
	 */

	for (i = 0; i < 16; i++) {
		cpx_done[i] = false;
	}
}

/*
 * Create graphics contexts.
 */
static void
make_gcs(struct sstate *s)
{
    XGCValues xgcv;

    if (appres.m3279) {
	int i;

	for (i = 0; i < NGCS; i++) {
	    if (s->gc[i] != (GC)None) {
		XtReleaseGC(toplevel, s->gc[i]);
		s->gc[i] = (GC)None;
	    }
	    if (s->gc[i + NGCS] != (GC)None) {
		XtReleaseGC(toplevel, s->gc[i + NGCS]);
		s->gc[i + NGCS] = (GC)None;
	    }
	    if (s->selgc[i] != (GC)None) {
		XtReleaseGC(toplevel, s->selgc[i]);
		s->selgc[i] = (GC)None;
	    }
	}
    } else {
	if (!appres.interactive.mono) {
	    make_gc_set(s, FA_INT_NORM_NSEL, normal_pixel, colorbg_pixel);
	    make_gc_set(s, FA_INT_NORM_SEL,  select_pixel, colorbg_pixel);
	    make_gc_set(s, FA_INT_HIGH_SEL,  bold_pixel, colorbg_pixel);
	} else {
	    make_gc_set(s, FA_INT_NORM_NSEL, xappres.foreground,
		    xappres.background);
	    make_gc_set(s, FA_INT_NORM_SEL,  xappres.foreground,
		    xappres.background);
	    make_gc_set(s, FA_INT_HIGH_SEL,  xappres.foreground,
		    xappres.background);
	}
    }
    if (s->clrselgc != (GC)None) {
	XtReleaseGC(toplevel, s->clrselgc);
	s->clrselgc = (GC)None;
    }
    xgcv.foreground = selbg_pixel;
    s->clrselgc = XtGetGC(toplevel, GCForeground, &xgcv);

    /* Create monochrome block cursor GC. */
    if (appres.interactive.mono && s->mcgc == (GC)None) {
	if (screen_depth > 1) {
	    xgcv.function = GXinvert;
	} else {
	    xgcv.function = GXxor;
	}
	xgcv.foreground = 1L;
	s->mcgc = XtGetGC(toplevel, GCForeground|GCFunction, &xgcv);
    }

    /* Create explicit cursor color cursor GCs. */
    if (xappres.use_cursor_color) {
	if (s->ucgc != (GC)None) {
	    XtReleaseGC(toplevel, s->ucgc);
	    s->ucgc = (GC)None;
	}
	xgcv.foreground = cursor_pixel;
	s->ucgc = XtGetGC(toplevel, GCForeground, &xgcv);

	if (s->invucgc != (GC)None) {
	    XtReleaseGC(toplevel, s->invucgc);
	    s->invucgc = (GC)None;
	}
	xgcv.foreground = colorbg_pixel;
	xgcv.background = cursor_pixel;
	xgcv.font = s->fid;
	s->invucgc = XtGetGC(toplevel, GCForeground|GCBackground|GCFont,
		&xgcv);
    }

    /* Set the flag for overstriking bold. */
    s->overstrike = (s->char_width > 1);
}

/* Set up a default color scheme. */
static void
default_color_scheme(void)
{
	static int default_attrib_colors[4] = {
	    GC_NONDEFAULT | HOST_COLOR_GREEN,	/* default */
	    GC_NONDEFAULT | HOST_COLOR_RED,	/* intensified */
	    GC_NONDEFAULT | HOST_COLOR_BLUE,	/* protected */
	    GC_NONDEFAULT | HOST_COLOR_WHITE	/* protected, intensified */
	};
	int i;

	ibm_fb = FB_WHITE;
	for (i = 0; i < 16; i++) {
		XtFree(color_name[i]);
		color_name[i] = XtNewString("white");
	}
	for (i = 0; i < 4; i++)
		field_colors[i] = default_attrib_colors[i];
}

/* Transfer the colorScheme resource into arrays. */
static bool
xfer_color_scheme(char *cs, bool do_popup)
{
    int i;
    char *scheme_name = NULL;
    char *s0 = NULL, *scheme = NULL;
    char *tk;

    char *tmp_color_name[16];
    enum fallback_color tmp_ibm_fb = FB_WHITE;
    char *tmp_colorbg_name = NULL;
    char *tmp_selbg_name = NULL;
    int tmp_field_colors[4];

    if (cs == NULL) {
	goto failure;
    }
    scheme_name = xs_buffer("%s.%s", ResColorScheme, cs);
    s0 = get_resource(scheme_name);
    if (s0 == NULL) {
	if (do_popup) {
	    popup_an_error("Can't find resource %s", scheme_name);
	} else {
	    xs_warning("Can't find resource %s", scheme_name);
	}
	goto failure;
    }
    scheme = s0 = XtNewString(s0);
    for (i = 0; (tk = strtok(scheme, " \t\n")) != NULL; i++) {
	scheme = NULL;
	if (i > 22) {
	    popup_an_error("Ignoring excess data in %s resource", scheme_name);
	    break;
	}
	switch (i) {
	case  0: case  1: case  2: case  3:
	case  4: case  5: case  6: case  7:
	case  8: case  9: case 10: case 11:
	case 12: case 13: case 14: case 15:	/* IBM color name */
	    tmp_color_name[i] = tk;
	    break;
	case 16:	/* default for IBM colors */
	    if (!strcmp(tk, "white")) {
		tmp_ibm_fb = FB_WHITE;
	    } else if (!strcmp(tk, "black")) {
		tmp_ibm_fb = FB_BLACK;
	    } else {
		if (do_popup) {
		    popup_an_error("Invalid default color");
		} else {
		    xs_warning("Invalid default color");
		}
		goto failure;
	    }
	    break;
	case 17:	/* screen background */
	    tmp_colorbg_name = tk;
	    break;
	case 18:	/* select background */
	    tmp_selbg_name = tk;
	    break;
	case 19: case 20: case 21: case 22:	/* attribute colors */
	    tmp_field_colors[i-19] = atoi(tk);
	    if (tmp_field_colors[i-19] < 0 ||
		tmp_field_colors[i-19] > 0x0f) {
		if (do_popup) {
		    popup_an_error("Invalid %s resource, ignoring",
			    scheme_name);
		} else {
		    xs_warning("Invalid %s resource, ignoring", scheme_name);
		}
		goto failure;
	    }
	    tmp_field_colors[i-19] |= GC_NONDEFAULT;
	}
    }
    if (i < 23) {
	if (do_popup) {
	    popup_an_error("Insufficient data in %s resource",
		scheme_name);
	} else {
	    xs_warning("Insufficient data in %s resource",
		scheme_name);
	}
	goto failure;
    }

    /* Success: transfer to live variables. */
    for (i = 0; i < 16; i++) {
	XtFree(color_name[i]);
	color_name[i] = XtNewString(tmp_color_name[i]);
    }
    ibm_fb = tmp_ibm_fb;
    xappres.colorbg_name = XtNewString(tmp_colorbg_name);
    xappres.selbg_name = XtNewString(tmp_selbg_name);
    for (i = 0; i < 4; i++) {
	field_colors[i] = tmp_field_colors[i];
    }

    /* Clean up and exit. */
    XtFree(scheme_name);
    XtFree(s0);
    return true;

failure:
    XtFree(scheme_name);
    XtFree(s0);
    return false;
}

/* Look up a GC, allocating it if necessary. */
static GC
get_gc(struct sstate *s, int color)
{
    int pixel_index;
    XGCValues xgcv;
    GC r;
    static bool in_gc_error = false;

    if (color & GC_NONDEFAULT) {
	color &= ~GC_NONDEFAULT;
    } else {
	color = (color & INVERT_MASK) | DEFAULT_PIXEL;
    }

    if ((r = s->gc[color]) != (GC)None) {
	return r;
    }

    /* Allocate the pixel. */
    pixel_index = PIXEL_INDEX(color);
    if (!cpx_done[pixel_index]) {
	if (!alloc_color(color_name[pixel_index], ibm_fb, &cpx[pixel_index])) {
	    if (!in_gc_error) {
		in_gc_error = true;
		popup_an_error("Cannot allocate colormap \"%s\" for 3279 "
			"color %d (%s), using \"%s\"",
			color_name[pixel_index],
			pixel_index,
			see_color((unsigned char)(pixel_index + 0xf0)),
			fb_name(ibm_fb));
		in_gc_error = false;
	    }
	}
	cpx_done[pixel_index] = true;
    }

    /* Allocate the GC. */
    xgcv.font = s->fid;
    if (!(color & INVERT_MASK)) {
	xgcv.foreground = cpx[pixel_index];
	xgcv.background = colorbg_pixel;
    } else {
	xgcv.foreground = colorbg_pixel;
	xgcv.background = cpx[pixel_index];
    }
    if (s == &nss && pixel_index == DEFAULT_PIXEL) {
	xgcv.graphics_exposures = true;
	r = XtGetGC(toplevel,
		GCForeground|GCBackground|GCFont|GCGraphicsExposures,
		&xgcv);
    } else {
	r = XtGetGC(toplevel,
		GCForeground|GCBackground|GCFont,
		&xgcv);
    }
    return s->gc[color] = r;
}

/* Look up a selection GC, allocating it if necessary. */
static GC
get_selgc(struct sstate *s, int color)
{
    XGCValues xgcv;
    GC r;

    if (color & GC_NONDEFAULT) {
	color = PIXEL_INDEX(color);
    } else {
	color = DEFAULT_PIXEL;
    }

    if ((r = s->selgc[color]) != (GC)None) {
	return r;
    }

    /* Allocate the pixel. */
    if (!cpx_done[color]) {
	if (!alloc_color(color_name[color], FB_WHITE, &cpx[color])) {
	    popup_an_error("Cannot allocate colormap \"%s\" for 3279 color "
		    "%d (%s), using \"white\"",
		    color_name[color], color,
		    see_color((unsigned char)(color + 0xf0)));
	    }
	cpx_done[color] = true;
    }

    /* Allocate the GC. */
    xgcv.font = s->fid;
    xgcv.foreground = cpx[color];
    xgcv.background = selbg_pixel;
    return s->selgc[color] =
	XtGetGC(toplevel, GCForeground|GCBackground|GCFont, &xgcv);
}

/* External entry points for GC allocation. */

GC
screen_gc(int color)
{
	return get_gc(ss, color | GC_NONDEFAULT);
}

GC
screen_invgc(int color)
{
	return get_gc(ss, INVERT_COLOR(color | GC_NONDEFAULT));
}
 
/*
 * Preallocate a set of graphics contexts for a given color.
 *
 * This logic is used only in pseudo-color mode.  In full color mode,
 * GCs are allocated dynamically by get_gc().
 */
static void
make_gc_set(struct sstate *s, int i, Pixel fg, Pixel bg)
{
	XGCValues xgcv;

	if (s->gc[i] != (GC)None)
		XtReleaseGC(toplevel, s->gc[i]);
	xgcv.foreground = fg;
	xgcv.background = bg;
	xgcv.graphics_exposures = true;
	xgcv.font = s->fid;
	if (s == &nss && !i)
		s->gc[i] = XtGetGC(toplevel,
		    GCForeground|GCBackground|GCFont|GCGraphicsExposures,
		    &xgcv);
	else
		s->gc[i] = XtGetGC(toplevel,
		    GCForeground|GCBackground|GCFont, &xgcv);
	if (s->gc[NGCS + i] != (GC)None)
		XtReleaseGC(toplevel, s->gc[NGCS + i]);
	xgcv.foreground = bg;
	xgcv.background = fg;
	s->gc[NGCS + i] = XtGetGC(toplevel, GCForeground|GCBackground|GCFont,
	    &xgcv);
	if (!appres.interactive.mono) {
		if (s->selgc[i] != (GC)None)
			XtReleaseGC(toplevel, s->selgc[i]);
		xgcv.foreground = fg;
		xgcv.background = selbg_pixel;
		s->selgc[i] = XtGetGC(toplevel,
		    GCForeground|GCBackground|GCFont, &xgcv);
	}
}


/*
 * Convert an attribute to a color index.
 */
static int
fa_color(unsigned char fa)
{
#    define DEFCOLOR_MAP(f) \
		((((f) & FA_PROTECT) >> 4) | (((f) & FA_INT_HIGH_SEL) >> 3))

    if (appres.m3279) {
	/*
	 * Color indices are the low-order 4 bits of a 3279 color
	 * identifier (0 through 15)
	 */
	if (appres.modified_sel && FA_IS_MODIFIED(fa)) {
	    return GC_NONDEFAULT | (xappres.modified_sel_color & 0xf);
	} else if (xappres.visual_select &&
		 FA_IS_SELECTABLE(fa) &&
		 !FA_IS_INTENSE(fa)) {
	    return GC_NONDEFAULT | (xappres.visual_select_color & 0xf);
	} else {
	    return field_colors[DEFCOLOR_MAP(fa)];
	}
    } else {
	/*
	 * Color indices are the intensity bits (0 through 2)
	 */
	if (FA_IS_ZERO(fa) || (appres.modified_sel && FA_IS_MODIFIED(fa))) {
	    return GC_NONDEFAULT | FA_INT_NORM_SEL;
	} else {
	    return GC_NONDEFAULT | (fa & 0x0c);
	}
    }
}



/*
 * Event handlers for toplevel FocusIn, FocusOut, KeymapNotify and
 * PropertyChanged events.
 */

static bool toplevel_focused = false;
static bool keypad_entered = false;

void
PA_Focus_xaction(Widget w _is_unused, XEvent *event, String *params _is_unused,
	Cardinal *num_params _is_unused)
{
    XFocusChangeEvent *fe = (XFocusChangeEvent *)event;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_Focus_xaction, event, params, num_params);
#endif /*]*/
    switch (fe->type) {
    case FocusIn:
	if (fe->detail != NotifyPointer) {
	    toplevel_focused = true;
	    screen_focus(true);
	}
	break;
    case FocusOut:
	toplevel_focused = false;
	if (!toplevel_focused && !keypad_entered) {
	    screen_focus(false);
	}
	break;
    }
}

void
PA_EnterLeave_xaction(Widget w _is_unused, XEvent *event _is_unused,
	String *params _is_unused, Cardinal *num_params _is_unused)
{
    XCrossingEvent *ce = (XCrossingEvent *)event;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_EnterLeave_xaction, event, params, num_params);
#endif /*]*/
    switch (ce->type) {
    case EnterNotify:
	keypad_entered = true;
	screen_focus(true);
	break;
    case LeaveNotify:
	keypad_entered = false;
	if (!toplevel_focused && !keypad_entered) {
	    screen_focus(false);
	}
	break;
    }
}

void
PA_KeymapNotify_xaction(Widget w _is_unused, XEvent *event,
	String *params _is_unused, Cardinal *num_params _is_unused)
{
    XKeymapEvent *k = (XKeymapEvent *)event;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_KeymapNotify_xaction, event, params, num_params);
#endif /*]*/
    shift_event(state_from_keymap(k->key_vector));
}

static void
query_window_state(void)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long leftover;
	unsigned char *data = NULL;
	static bool was_up = false;

	if (XGetWindowProperty(display, XtWindow(toplevel), a_state, 0L,
	    (long)BUFSIZ, false, a_state, &actual_type, &actual_format,
	    &nitems, &leftover, &data) != Success)
		return;
	if (actual_type == a_state && actual_format == 32) {
		if (*(unsigned long *)data == IconicState) {
			iconic = true;
			keypad_popdown(&was_up);
		} else {
			iconic = false;
			invert_icon(false);
			keypad_first_up();
			if (was_up) {
			    	keypad_popup();
				was_up = false;
			}
		}
	}
	XFree(data);
}

void
PA_StateChanged_xaction(Widget w _is_unused, XEvent *event, String *params,
	Cardinal *num_params)
{
#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_StateChanged_xaction, event, params, num_params);
#endif /*]*/
    query_window_state();
}

/*
 * Handle Shift events (KeyPress and KeyRelease events, or KeymapNotify events
 * that occur when the mouse enters the window).
 */

void
shift_event(int event_state)
{
    static int old_state;
    bool shifted_now =
	(event_state & (ShiftKeyDown | MetaKeyDown | AltKeyDown)) != 0;

    if (event_state != old_state) {
	old_state = event_state;
	status_shift_mode(event_state);
	if (shifted != shifted_now) {
	    shifted = shifted_now;
	    keypad_shift();
	}
    }
}

/*
 * Handle the mouse entering and leaving the window.
 */
static void
screen_focus(bool in)
{
	/*
	 * Update the input context focus.
	 */
	if (ic != NULL) {
		if (in)
			XSetICFocus(ic);
		else
			XUnsetICFocus(ic);
	}

	/*
	 * Cancel any pending cursor blink.  If we just came into focus and
	 * have a blinking cursor, we will start a fresh blink cycle below, so
	 * the filled-in cursor is visible for a full turn.
	 */
	cancel_blink();

	/*
	 * If the cursor is disabled, simply change internal state.
	 */
	if (!CONNECTED) {
		in_focus = in;
		return;
	}

	/*
	 * Change the appearance of the cursor.  Make it hollow out or fill in
	 * instantly, even if it was blinked off originally.
	 */
	(void) cursor_off();
	in_focus = in;
	cursor_on();

	/*
	 * If we just came into focus and we're supposed to have a blinking
	 * cursor, schedule a blink.
	 */
	if (in_focus && toggled(CURSOR_BLINK))
		schedule_cursor_blink();
}

/*
 * Change fonts.
 */
static bool
SetFont_action(ia_t ia, unsigned argc, const char **argv)
{
    action_debug("SetFont", ia, argc, argv);
    if (check_argc("SetFont", argc, 1, 1) < 0) {
	return false;
    }

    screen_newfont(argv[0], true, false);
    return true;
}

/*
 * Split an emulatorFontList resource entry, which looks like:
 *  [menu-name:] [#noauto] [#resize] font-name
 * Modifies the input string.
 */
static void
split_font_list_entry(char *entry, char **menu_name, bool *noauto,
    bool *resize, char **font_name)
{
	char *colon;
	char *s;
	bool any = false;

	if (menu_name != NULL)
		*menu_name = NULL;
	if (noauto != NULL)
		*noauto = false;
	if (resize != NULL)
		*resize = false;

	colon = strchr(entry, ':');
	if (colon != NULL) {
		if (menu_name != NULL)
			*menu_name = entry;
		*colon = '\0';
		s = colon + 1;
	} else
		s = entry;

	do {
		any = false;
		while (isspace(*s))
			s++;
		if (!strncmp(s, "#noauto", 7) &&
		    (!s[7] || isspace(s[7]))) {
			if (noauto != NULL)
				*noauto = true;
			s += 7;
			any = true;
		} else if (!strncmp(s, "#resize", 7) &&
			   (!s[7] || isspace(s[7]))) {
			if (resize != NULL)
				*resize = true;
			s += 7;
			any = true;
		}
	} while (any);

	*font_name = s;
}

/*
 * Load a font with a display character set required by a charset.
 * Returns true for success, false for failure.
 * If it succeeds, the caller is responsible for calling:
 *	screen_reinit(FONT_CHANGE)
 */
bool
screen_new_display_charsets(const char *realname, const char *csnames)
{
    char *rl;
    char *s0, *s;
    char *fontname = NULL;
    char *lff;
    bool font_found = false;
    const char *display_charsets;
    const char *dbcs_display_charsets;

    /* Handle the default. */
    if (realname == NULL) {
	/* Handle the default. */
	display_charsets = default_display_charset;
    } else {
	/* Look up the display character set(s). */
	display_charsets = lookup_display_charset(realname);
	assert(display_charsets != NULL);
	dbcs_display_charsets = lookup_display_charset_dbcs(realname);
	if (dbcs_display_charsets != NULL) {
	    display_charsets = lazyaf("%s+%s", display_charsets,
		    dbcs_display_charsets);
	}
    }

    /*
     * If the emulator fonts already implement those charsets, we're done.
     */
    if (efont_charset != NULL && !strcmp(display_charsets, efont_charset)) {
	goto done;
    }

    /*
     * If the user chose an emulator font, but we haven't tried it yet,
     * see if it implements the right charset.
     */
    if (efontname == NULL && xappres.efontname != NULL) {
	lff = load_fixed_font(xappres.efontname, display_charsets);
	if (lff != NULL) {
	    if (strcmp(xappres.efontname, "3270")) {
		popup_an_error("%s", lff);
	    }
	    Free(lff);
	} else {
	    fontname = xappres.efontname;
	}
    }

    /*
     * Otherwise, try to get a font from the resize lists.
     */
    if (fontname == NULL) {
	rl = get_fresource("%s.%s", ResEmulatorFontList, display_charsets);
	if (rl != NULL) {
	    s0 = s = NewString(rl);
	    while (!font_found && split_lresource(&s, &fontname) == 1) {
		bool noauto = false;
		char *fn = NULL;

		split_font_list_entry(fontname, NULL, &noauto, NULL, &fn);
		if (noauto || !*fn) {
		    continue;
		}

		lff = load_fixed_font(fn, display_charsets);
		if (lff != NULL) {
		    Free(lff);
		} else {
		    font_found = true;
		}
	    }
	    Free(s0);
	}

	if (!font_found &&
	    (!strcasecmp(display_charsets, default_display_charset) ||
	     !strcasecmp(display_charsets, "iso8859-1"))) {
	    /* Try "fixed". */
	    if ((lff = load_fixed_font("!fixed", display_charsets)) == NULL) {
		font_found = true;
	    } else {
		/* Fatal. */
		xs_error("%s", lff);
		Free(lff);
		/*NOTREACHED*/
		return false;
	    }
	}

	if (!font_found) {
	    char *cs_dup;
	    char *cs;
	    char *buf;
	    char *lasts = NULL;

	    if (strchr(display_charsets, '+') != NULL) {
		/*
		 * Despite what the code below appears to be
		 * able to do, we don't know how to search for a
		 * DBCS font.  Bail here.
		 */
		return false;
	    }

	    buf = cs_dup = NewString(display_charsets);
	    while (!font_found &&
		   (cs = strtok_r(buf, ",", &lasts)) != NULL) {
		char *part1 = NULL, *part2 = NULL;
		int n_parts = 1;

		buf = NULL;
		n_parts = split_dbcs_resource(cs, '+', &part1, &part2);

		if (n_parts == 1 && !strncasecmp(cs, "3270cg", 6)) {
		    free(part1);
		    continue;
		}

		lff = load_fixed_font(NULL, cs);
		if (lff != NULL) {
		    Free(lff);
		} else {
		    font_found = true;
		}
		if (part1 != NULL) {
		    Free(part1);
		}
		if (part2 != NULL) {
		    Free(part2);
		}
	    }
	    Free(cs_dup);
	}

	if (!font_found) {
	    char *xs = expand_cslist(display_charsets);

	    popup_an_error("No %s fonts found", xs);
	    Free(xs);
	    return false;
	}
    }
    allow_resize = xappres.allow_resize;

done:
    /* Set the appropriate global. */
    Replace(required_display_charsets,
	    display_charsets? NewString(display_charsets): NULL);
    init_rsfonts(required_display_charsets);

    return true;
}

void
screen_newfont(const char *fontnames, bool do_popup, bool is_cs)
{
	char *old_font;
	char *lff;

	/* Do nothing, successfully. */
	if (!is_cs && efontname && !strcmp(fontnames, efontname))
		return;

	/* Save the old font before trying the new one. */
	old_font = XtNewString(efontname);

	/* Try the new one. */
	if ((lff = load_fixed_font(fontnames, required_display_charsets)) != NULL) {
		if (do_popup)
			popup_an_error("%s", lff);
		Free(lff);
		XtFree(old_font);
		return;
	}

	/* Store the old name away, in case we have to go back to it. */
	Replace(redo_old_font, old_font);
	screen_redo = REDO_FONT;

	screen_reinit(FONT_CHANGE);
	efont_changed = true;
}

/*
 * Expand a character set list into English.
 */
static char *
expand_cslist(const char *s)
{
	int commas = 0;
	const char *t;
	char *comma;
	char *r;

	/* Count the commas. */
	for (t = s; (comma = strchr(t, ',')) != NULL; t = comma + 1) {
		commas++;
	}

	/* If there aren't any, there isn't any work to do. */
	if (!commas)
		return NewString(s);

	/* Allocate enough space for "a, b, c or d". */
	r = Malloc(strlen(s) + (commas * 2) + 2 + 1);
	*r = '\0';

	/* Copy and expand. */
	for (t = s; (comma = strchr(t, ',')) != NULL; t = comma + 1) {
		int wl = comma - t;

		if (*r)
			(void) strcat(r, ", ");
		(void) strncat(r, t, wl);
	}
	return strcat(strcat(r, " or "), t);
}

/*
 * Load and query a font.
 * Returns NULL (okay) or an error message.
 */
static char *
load_fixed_font(const char *names, const char *reqd_display_charsets)
{
	int num_names = 1, num_cs = 1;
	char *name1 = NULL, *name2 = NULL;
	char *charset1 = NULL, *charset2 = NULL;
	char *r;

#if defined(DEBUG_FONTPICK) /*[*/
	fprintf(stderr, "load_fixed_font(%s, %s)\n",
	    names? names: "(wild)", reqd_display_charsets);
#endif /*]*/

	/* Split out the names and character sets. */
	if (names)
		num_names = split_dbcs_resource(names, '+', &name1, &name2);
	num_cs = split_dbcs_resource(reqd_display_charsets, '+', &charset1,
			&charset2);
	if (!names)
	    	num_names = num_cs;
	if (num_names == 1 && num_cs >= 2) {
		Free(name1);
		Free(name2);
		Free(charset1);
		Free(charset2);
		return NewString("Must specify two font names (SBCS+DBCS)");
	}
	if (num_names == 2 && num_cs < 2) {
		Free(name2);
		name2 = NULL;
	}

	/* If there's a DBCS font, load that first. */
	if (name2 != NULL) {
		/* Load the second font. */
		r = lff_single(name2, charset2, true);
		if (r != NULL) {
			Free(name1);
			Free(charset1);
			return r;
		}
	} else {
		dbcs_font.font_struct = NULL;
		dbcs_font.font = None;
		dbcs = false;
	}

	/* Load the SBCS font. */
	r = lff_single(name1, charset1, false);

	/* Free the split-out names and return the final result. */
	Free(name1);
	Free(name2);
	Free(charset1);
	Free(charset2);
	return r;
}

static bool
charset_in_reqd(const char *charset, const char *reqd)
{
    	char *r = NewString(reqd);
	char *str = r;
    	char *tok;
	bool rv = false;

	while ((tok = strtok(str, ",")) != NULL) {
	    	str = NULL;
		if (!strcasecmp(charset, tok)) {
		    	rv = true;
			break;
		}
	}
	Free(r);
	return rv;
}

/*
 * Load and query one font.
 * Returns NULL (okay) or an error message.
 */
static char *
lff_single(const char *name, const char *reqd_display_charset, bool is_dbcs)
{
	XFontStruct *g;
	const char *best = NULL;

#if defined(DEBUG_FONTPICK) /*[*/
	fprintf(stderr, "lff_single: name %s, cs %s, %s\n",
		name? name: "(wild)",
		reqd_display_charset, is_dbcs? "dbcs": "sbcs");
#endif /*]*/

	if (name && *name == '!') {
		name++;
	}

	if (name) {
	    	char **names;
		int count;
		XFontStruct *f;
		unsigned long svalue;
		char *family_name, *font_encoding, *fe, *charset;

	    	if (!dfc_search_name(name))
			return xs_buffer("Font %s\nnot found", name);

		/* Check the character set */
		names = XListFontsWithInfo(display, name, 1, &count, &f);
		if (names == NULL)
			return xs_buffer("Font %s\nnot found", name);
		if (XGetFontProperty(f, a_registry, &svalue))
			family_name = XGetAtomName(display, svalue);
		else {
			XFreeFontInfo(names, f, count);
		    	return xs_buffer("Font %s\nhas no registry property",
				name);
		}
		if (XGetFontProperty(f, a_encoding, &svalue))
			font_encoding = XGetAtomName(display, svalue);
		else {
			XFreeFontInfo(names, f, count);
		    	return xs_buffer("Font %s\nhas no encoding property",
				name);
		}
		if (font_encoding[0] == '-')
			fe = font_encoding + 1;
		else
			fe = font_encoding;
		XFreeFontInfo(names, f, count);
		charset = xs_buffer("%s-%s", family_name, fe);
		Free(family_name);
		Free(font_encoding);
		if (!charset_in_reqd(charset, reqd_display_charset)) {
		    	char *r = xs_buffer("Font %s\nimplements %s, not %s\n",
				name, charset, reqd_display_charset);

			Free(charset);
			return r;

		}
		Free(charset);

		best = name;
	} else {
	    	void *cookie;
		dfc_t *d;
		int best_pixel_size = 0;
		char *best_weight = NULL;

		cookie = NULL;
		while (dfc_search_family(reqd_display_charset, &d, &cookie)) {
			if (best == NULL ||
			    (labs(d->points - 14) <
			     labs(best_pixel_size - 14)) ||
			    (best_weight == NULL ||
			     (!strcasecmp(best_weight, "bold") &&
			      strcasecmp(d->weight, "bold")))) {
				best = d->name;
				best_weight = d->weight;
				best_pixel_size = d->points;
			}
		}
		if (best == NULL) {
			return xs_buffer("No %s fonts found",
				reqd_display_charset);
		}
	}

	g = XLoadQueryFont(display, best);
	set_font_globals(g, /*name*/best, best, g->fid, is_dbcs);
	return NULL;
}

/*
 * Figure out what sort of registry and encoding we want.
 */
char *
display_charset(void)
{
	return (required_display_charsets != NULL)? required_display_charsets:
	    					 default_display_charset;
}

/*
 * Set globals based on font name and info
 */
static void
set_font_globals(XFontStruct *f, const char *ef, const char *fef, Font ff,
    bool is_dbcs)
{
	unsigned long svalue;
	unsigned i;
	char *family_name = NULL;
	char *font_encoding = NULL;
	char *fe = NULL;
	char *font_charset = NULL;

	if (XGetFontProperty(f, a_registry, &svalue))
		family_name = XGetAtomName(display, svalue);
	if (family_name == NULL)
	    	Error("Cannot get font family_name");
	if (XGetFontProperty(f, a_encoding, &svalue))
		font_encoding = XGetAtomName(display, svalue);
	if (font_encoding == NULL)
	    	Error("Cannot get font encoding");
	if (font_encoding[0] == '-')
		fe = font_encoding + 1;
	else
		fe = font_encoding;

	font_charset = xs_buffer("%s-%s", family_name, fe);
	Free(family_name);
	Free(font_encoding);

	if (is_dbcs) {
		/* Hack. */
		dbcs_font.font_struct = f;
		dbcs_font.font = f->fid;
		dbcs_font.unicode = !strcasecmp(family_name, "iso10646");
		dbcs_font.ascent = f->max_bounds.ascent;
		dbcs_font.descent = f->max_bounds.descent;
		dbcs_font.char_width  = fCHAR_WIDTH(f);
		dbcs_font.char_height = dbcs_font.ascent + dbcs_font.descent;
		dbcs_font.d16_ix = display16_init(font_charset);
		dbcs = true;
		Replace(full_efontname_dbcs, XtNewString(fef));
		Replace(efont_charset_dbcs, font_charset);
		return;
	}
	Replace(efontname, XtNewString(ef));
	Replace(full_efontname, XtNewString(fef));
	Replace(efont_charset, font_charset);

	/* Set the dimensions. */
	nss.char_width  = fCHAR_WIDTH(f);
	nss.char_height = fCHAR_HEIGHT(f);
	nss.fid = ff;
	if (nss.font != NULL)
		XFreeFontInfo(NULL, nss.font, 1);
	nss.font = f;
	nss.ascent = f->ascent;
	nss.descent = f->descent;

	/* Figure out if this is a 3270 font, or a standard X font. */
	if (XGetFontProperty(f, XA_FAMILY_NAME, &svalue))
		nss.standard_font = (Atom) svalue != a_3270;
	else if (!strncmp(efontname, "3270", 4))
		nss.standard_font = false;
	else
		nss.standard_font = true;

	/* Set other globals. */
	if (nss.standard_font) {
		nss.extended_3270font = false;
		nss.font_8bit = efont_matches;
		nss.font_16bit = (f->max_byte1 > 0);
		nss.d8_ix = display8_init(nss.font_8bit? font_charset:
							 "ascii-7");
	} else {
#if defined(BROKEN_MACH32)
		nss.extended_3270font = false;
#else
		nss.extended_3270font = f->max_byte1 > 0 ||
			f->max_char_or_byte2 > 255;
#endif
		nss.font_8bit = false;
		nss.font_16bit = false;
		nss.d8_ix = display8_init(font_charset);
	}

	/* See if this font has any unusually-shaped characters. */
	INIT_ODD(nss.odd_width);
	INIT_ODD(nss.odd_lbearing);
	nss.funky_font = false;
	if (!nss.extended_3270font && f->per_char != NULL) {
		for (i = 0; i < 256; i++) {
			if (PER_CHAR(f, i).width == 0 &&
			    (PER_CHAR(f, i).rbearing |
			     PER_CHAR(f, i).lbearing |
			     PER_CHAR(f, i).ascent |
			     PER_CHAR(f, i).descent) == 0) {
				/* Missing character. */
				continue;
			}

			if (PER_CHAR(f, i).width != f->max_bounds.width) {
				SET_ODD(nss.odd_width, i);
				nss.funky_font = true;
			}
			if (PER_CHAR(f, i).lbearing < 0) {
				SET_ODD(nss.odd_lbearing, i);
				nss.funky_font = true;
			}
		}
	}

	/*
	 * If we've changed the rules for resizing, let the window manager
	 * know.
	 */
	if (container != NULL)
		set_toplevel_sizes();
}

/*
 * Font initialization.
 */
void
font_init(void)
{
}

/*
 * Change models.
 */
void
screen_change_model(int mn, int ovc, int ovr)
{
	if (CONNECTED ||
	    (model_num == mn && ovc == ov_cols && ovr == ov_rows))
		return;

	redo_old_model = model_num;
	redo_old_ov_cols = ov_cols;
	redo_old_ov_rows = ov_rows;
	screen_redo = REDO_MODEL;

	model_changed = true;
	if (ov_cols != ovc || ov_rows != ovr)
		oversize_changed = true;
	set_rows_cols(mn, ovc, ovr);
	st_changed(ST_REMODEL, true);
	screen_reinit(MODEL_CHANGE);
}

/*
 * Change emulation modes.
 */
void
screen_extended(bool extended _is_unused)
{
	set_rows_cols(model_num, ov_cols, ov_rows);
	model_changed = true;
}

void
screen_m3279(bool m3279 _is_unused)
{
	destroy_pixels();
	screen_reinit(COLOR_CHANGE);
	set_rows_cols(model_num, ov_cols, ov_rows);
	model_changed = true;
}

/*
 * Change color schemes.  Alas, this is destructive if it fails.
 */
void
screen_newscheme(char *s)
{
    bool xferred;

    if (!appres.m3279) {
	return;
    }

    destroy_pixels();
    xferred = xfer_color_scheme(s, true);
    if (xferred) {
	xappres.color_scheme = s;
    }
    screen_reinit(COLOR_CHANGE);
    scheme_changed = true;
}

/*
 * Change character sets.
 */
void
screen_newcharset(char *csname)
{
	char *old_charset = NewString(get_charset_name());

	switch (charset_init(csname)) {
	    case CS_OKAY:
		/* Success. */
		Free(old_charset);
		st_changed(ST_CHARSET, true);
		screen_reinit(CHARSET_CHANGE | FONT_CHANGE);
		charset_changed = true;
		break;
	    case CS_NOTFOUND:
		Free(old_charset);
		popup_an_error("Cannot find definition of host character set \"%s\"",
		    csname);
		break;
	    case CS_BAD:
		Free(old_charset);
		popup_an_error("Invalid charset definition for \"%s\"", csname);
		break;
	    case CS_PREREQ:
		Free(old_charset);
		popup_an_error("No fonts for host character set \"%s\"", csname);
		break;
	    case CS_ILLEGAL:
		/* Error already popped up. */
		Free(old_charset);
		break;

	}
}

/*
 * Visual or not-so-visual bell
 */
void
ring_bell(void)
{
    static XGCValues xgcv;
    static GC bgc;
    static int initted;
    struct timeval tv;

    /* Ring the real display's bell. */
    if (!appres.interactive.visual_bell) {
	XBell(display, xappres.bell_volume);
    }

    /* If we're iconic, invert the icon and return. */
    if (!xappres.active_icon) {
	query_window_state();
	if (iconic) {
	    invert_icon(true);
	    return;
	}
    }

    if (!appres.interactive.visual_bell || !ss->exposed_yet) {
	return;
    }

    /* Do a screen flash. */

    if (!initted) {
	xgcv.function = GXinvert;
	bgc = XtGetGC(toplevel, GCFunction, &xgcv);
	initted = 1;
    }
    screen_disp(false);
    XFillRectangle(display, ss->window, bgc,
	0, 0, ss->screen_width, ss->screen_height);
    XSync(display, 0);
    tv.tv_sec = 0;
    tv.tv_usec = 125000;
    (void) select(0, NULL, NULL, NULL, &tv);
    XFillRectangle(display, ss->window, bgc,
	0, 0, ss->screen_width, ss->screen_height);
    XSync(display, 0);
}

/*
 * Window deletion
 */
void
PA_WMProtocols_xaction(Widget w, XEvent *event, String *params,
	Cardinal *num_params)
{
    XClientMessageEvent *cme = (XClientMessageEvent *)event;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_WMProtocols_xaction, event, params, num_params);
#endif /*]*/
    if ((Atom)cme->data.l[0] == a_delete_me) {
	if (w == toplevel) {
	    x3270_exit(0);
	} else {
	    XtPopdown(w);
	}
    } else if ((Atom)cme->data.l[0] == a_save_yourself && w == toplevel) {
	save_yourself();
    }
}


/* Initialize the icon. */
void
icon_init(void)
{
    x3270_icon = XCreateBitmapFromData(display, root_window,
	    (char *)x3270_bits, x3270_width, x3270_height);

    if (xappres.active_icon) {
	Dimension iw, ih;

	aicon_font_init();
	aicon_size(&iw, &ih);
	icon_shell =  XtVaAppCreateShell(
		"x3270icon",
		"X3270",
		overrideShellWidgetClass,
		display,
		XtNwidth, iw,
		XtNheight, ih,
	    XtNmappedWhenManaged, False,
	    NULL);
	XtRealizeWidget(icon_shell);
	XtVaSetValues(toplevel,
	    XtNiconWindow, XtWindow(icon_shell),
	    NULL);
	if (xappres.active_icon) {
	    XtVaSetValues(icon_shell,
		    XtNbackground, appres.interactive.mono?
			xappres.background: colorbg_pixel,
		    NULL);
	}
    } else {
	unsigned i;

	for (i = 0; i < sizeof(x3270_bits); i++) {
	    x3270_bits[i] = ~x3270_bits[i];
	}
	inv_icon = XCreateBitmapFromData(display, root_window,
		(char *)x3270_bits, x3270_width, x3270_height);
	wait_icon = XCreateBitmapFromData(display, root_window,
		(char *)wait_bits, wait_width, wait_height);
	for (i = 0; i < sizeof(wait_bits); i++) {
	    wait_bits[i] = ~wait_bits[i];
	}
	inv_wait_icon = XCreateBitmapFromData(display, root_window,
		(char *)wait_bits, wait_width, wait_height);
	XtVaSetValues(toplevel,
		XtNiconPixmap, x3270_icon,
		XtNiconMask, x3270_icon,
		NULL);
    }
}

/*
 * Initialize the active icon font information.
 */
static void
aicon_font_init(void)
{
    XFontStruct *f;
    Font ff;
    char **matches;
    int count;

    if (!xappres.active_icon) {
	xappres.label_icon = False;
	return;
    }

    matches = XListFontsWithInfo(display, xappres.icon_font, 1, &count,
	    &f);
    if (matches == NULL) {
	popup_an_error("No font %s \"%s\"\nactiveIcon will not work",
		ResIconFont, xappres.icon_font);
	xappres.active_icon = False;
	return;
    }
    ff = XLoadFont(display, matches[0]);
    iss.char_width = fCHAR_WIDTH(f);
    iss.char_height = fCHAR_HEIGHT(f);
    iss.fid = ff;
    iss.font = f;
    iss.ascent = f->ascent;
    iss.overstrike = false;
    iss.standard_font = true;
    iss.extended_3270font = false;
    iss.font_8bit = false;
    iss.obscured = true;
    iss.d8_ix = display8_init("ascii-7");
    if (xappres.label_icon) {
	matches = XListFontsWithInfo(display, xappres.icon_label_font, 1,
		&count, &ailabel_font);
	if (matches == NULL) {
	    popup_an_error("Cannot load %s \"%s\" font\nlabelIcon will not "
		    "work", ResIconLabelFont, xappres.icon_label_font);
	    xappres.label_icon = False;
	    return;
	}
	ailabel_font->fid = XLoadFont(display, matches[0]);
	aicon_label_height = fCHAR_HEIGHT(ailabel_font) + 2;
    }
    INIT_ODD(iss.odd_width);
    INIT_ODD(iss.odd_lbearing);
    iss.funky_font = false;
}

/*
 * Determine the current size of the active icon.
 */
static void
aicon_size(Dimension *iw, Dimension *ih)
{
	XIconSize *is;
	int count;

	*iw = maxCOLS*iss.char_width + 2*VHALO;
	*ih = maxROWS*iss.char_height + 2*HHALO + aicon_label_height;
	if (XGetIconSizes(display, root_window, &is, &count)) {
		if (*iw > (unsigned) is[0].max_width)
			*iw = is[0].max_width;
		if (*ih > (unsigned) is[0].max_height)
			*ih = is[0].max_height;
	}
}

/*
 * Initialize the active icon.  Assumes that aicon_font_init has already been
 * called.
 */
static void
aicon_init(void)
{
    if (!xappres.active_icon) {
	return;
    }

    iss.widget = icon_shell;
    iss.window = XtWindow(iss.widget);
    iss.cursor_daddr = 0;
    iss.exposed_yet = false;
    if (xappres.label_icon) {
	XGCValues xgcv;

	xgcv.font = ailabel_font->fid;
	xgcv.foreground = xappres.foreground;
	xgcv.background = xappres.background;
	ailabel_gc = XtGetGC(toplevel, GCFont|GCForeground|GCBackground,
		&xgcv);
    }
}

/*
 * Reinitialize the active icon.
 */
static void
aicon_reinit(unsigned cmask)
{
    if (!xappres.active_icon) {
	return;
    }

    if (cmask & (FONT_CHANGE | COLOR_CHANGE)) {
	make_gcs(&iss);
    }

    if (cmask & MODEL_CHANGE) {
	aicon_size(&iss.screen_width, &iss.screen_height);
	Replace(iss.image, (union sp *)XtMalloc(sizeof(union sp) * maxROWS *
		    maxCOLS));
	XtVaSetValues(iss.widget,
		XtNwidth, iss.screen_width,
		XtNheight, iss.screen_height,
		NULL);
    }
    if (cmask & (MODEL_CHANGE | FONT_CHANGE | COLOR_CHANGE)) {
	(void)memset((char *)iss.image, 0, sizeof(union sp) * maxROWS *
		maxCOLS);
    }
}

/* Draw the aicon label */
static void
draw_aicon_label(void)
{
	int len;
	Position x;

	if (!xappres.label_icon || !iconic)
		return;

	XFillRectangle(display, iss.window,
	    get_gc(&iss, INVERT_COLOR(0)),
	    0, iss.screen_height - aicon_label_height,
	    iss.screen_width, aicon_label_height);
	len = strlen(aicon_text);
	x = ((int)iss.screen_width - XTextWidth(ailabel_font, aicon_text, len))
	     / 2;
	if (x < 0)
		x = 2;
	XDrawImageString(display, iss.window, ailabel_gc,
	    x,
	    iss.screen_height - aicon_label_height + ailabel_font->ascent,
	    aicon_text, len);
}

/* Set the aicon label */
void
set_aicon_label(char *l)
{
    Replace(aicon_text, XtNewString(l));
    draw_aicon_label();
}

/* Change the bitmap icon. */
static void
flip_icon(bool inverted, enum mcursor_state mstate)
{
    Pixmap p = x3270_icon;

    if (mstate == LOCKED) {
	mstate = NORMAL;
    }
    if (xappres.active_icon
	|| (inverted == icon_inverted && mstate == icon_cstate)) {
	return;
    }
    switch (mstate) {
    case WAIT:
	if (inverted) {
	    p = inv_wait_icon;
	} else {
	    p = wait_icon;
	}
	break;
    case LOCKED:
    case NORMAL:
	if (inverted) {
	    p = inv_icon;
	} else {
	    p = x3270_icon;
	}
	break;
    }
    XtVaSetValues(toplevel,
	    XtNiconPixmap, p,
	    XtNiconMask, p,
	    NULL);
    icon_inverted = inverted;
    icon_cstate = mstate;
}

/*
 * Invert the icon.
 */
static void
invert_icon(bool inverted)
{
	flip_icon(inverted, icon_cstate);
}

/*
 * Change to the lock icon.
 */
static void
lock_icon(enum mcursor_state state)
{
	flip_icon(icon_inverted, state);
}


/* Check the font menu for an existing name. */
static bool
font_in_menu(const char *font)
{
	struct font_list *g;

	for (g = font_list; g != NULL; g = g->next) {
		if (!strcasecmp(NO_BANG(font), NO_BANG(g->font)))
			return true;
	}
	return false;
}

/* Add a font to the font menu. */
static bool
add_font_to_menu(char *label, const char *font)
{
	struct font_list *f;

	label = NewString(label);
	f = (struct font_list *)XtMalloc(sizeof(*f));
	if (!split_hier(label, &f->label, &f->parents)) {
		Free((XtPointer)f);
		return false;
	}
	f->font = NewString(font);
	f->next = NULL;
	f->mlabel = label;
	if (font_list)
		font_last->next = f;
	else
		font_list = f;
	font_last = f;
	font_count++;
	return true;
}

/*
 * Resize font list parser.
 */
static void
init_rsfonts(char *charset_name)
{
	char *ms;
	struct rsfont *r;
	struct font_list *f;
	char *dupcsn, *csn, *buf;
	char *lasts = NULL;
	XFontStruct *fs;
	char *hier_name;

	/* Clear the old lists. */
	while (rsfonts != NULL) {
		r = rsfonts->next;
		Free(rsfonts);
		rsfonts = r;
	}
	while (font_list != NULL) {
		f = font_list->next;
		if (font_list->parents)
			Free(font_list->parents);
		Free(font_list->mlabel);
		Free(font_list->font);
		Free(font_list);
		font_list = f;
	}
	font_last = NULL;
	font_count = 0;

	/* If there's no character set, we're done. */
	if (charset_name == NULL)
		return;

	/* Get the emulatorFontList resource. */
	ms = get_fresource("%s.%s", ResEmulatorFontList, charset_name);
	if (ms != NULL) {
		char *ns;
		char *line;
		char *label;
		char *font;
		bool resize;
		char **matches;
		int count;

		ns = ms = NewString(ms);
		while (split_lresource(&ms, &line) == 1) {

			/* Figure out what it's about. */
			split_font_list_entry(line, &label, NULL, &resize,
			    &font);
			if (!*font)
				continue;

			/* Search for duplicates. */
			if (font_in_menu(font))
				continue;

			/* Add it to the font_list (menu). */
			if (!add_font_to_menu((label != NULL)? label:
						NO_BANG(font),
					    font))
				continue;

			/* Add it to the resize menu, if possible. */
			if (!resize)
				continue;
			matches = XListFontsWithInfo(display, NO_BANG(font), 1,
						     &count, &fs);
			if (matches == NULL)
				continue;
			r = (struct rsfont *)XtMalloc(sizeof(*r));
			r->name = XtNewString(font);
			r->width = fCHAR_WIDTH(fs);
			r->height = fCHAR_HEIGHT(fs);
			XFreeFontInfo(matches, fs, count);
			r->next = rsfonts;
			rsfonts = r;
		}
		free(ns);
	}

	/*
	 * In DBCS mode, if we've found at least one appropriate font from the
	 * list, we're done.
	 */
	if (dbcs) {
		return;
	}

	/* Add 'fixed' to the menu, so there's at least one alternative. */
	(void) add_font_to_menu("fixed", "!fixed");

	/* Expand out wild-cards based on the display character set names. */
	buf = dupcsn = NewString(charset_name);
	while ((csn = strtok_r(buf, ",", &lasts)) != NULL) {
	    	void *cookie;
		const char *name;

		buf = NULL;
	    	if (!strncasecmp(csn, "3270cg", 6))
		    	continue;

		cookie = NULL;
		while ((name = dfc_search_family(csn, NULL, &cookie))
			    != NULL) {
			if (!font_in_menu(name)) {
				char *dash1 = NULL, *dash2 = NULL;

				if (name[0] == '-') {
					dash1 = strchr(name + 1, '-');
					if (dash1 != NULL)
						dash2 = strchr(dash1 + 1, '-');
				}
				if (dash2 != NULL) {
					hier_name =
					    xs_buffer("%s>%.*s>%s",
						csn,
						(int)(dash2 - name - 1),
						 name + 1,
						dash2 + 1);
				} else
					hier_name = xs_buffer("%s>%s",
							csn, name);
				(void) add_font_to_menu(hier_name, name);
				Free(hier_name);
			}
		}
	}
	Free(dupcsn);
}

/*
 * Handle ConfigureNotify events.
 */
static struct {
    bool ticking;
    Dimension width, height;
    Position x, y;
    XtIntervalId id;
} cn = {
    false, 0, 0, 0, 0, 0
};
static Position main_x = 0, main_y = 0;

/*
 * Timeout routine called 0.5 sec after x3270 sets new screen dimensions.
 * We assume that if this happens, the window manager is happy with our new
 * size.
 */
static void
configure_stable(XtPointer closure _is_unused, XtIntervalId *id _is_unused)
{
	vtrace("Reconfigure timer expired\n");
	configure_ticking = false;
	if (!cn.ticking)
		screen_redo = REDO_NONE;
}

/* Perform a resize operation. */
static void
do_resize(void)
{
    struct rsfont *r;
    struct rsfont *best = (struct rsfont *) NULL;

    /* What we're doing now is irreversible. */
    screen_redo = REDO_RESIZE;

    if (rsfonts == NULL || !allow_resize) {
	/* Illegal or impossible. */
	if (rsfonts == NULL) {
	    vtrace("  no fonts available for resize\n"
		    "    reasserting previous size\n");
	} else {
	    vtrace("  resize prohibited by resource\n"
		    "    reasserting previous size\n");
	}
	set_toplevel_sizes();
	return;
    }

    /*
     * Recompute the resulting screen area for each font, based on the
     * current keypad, model, and scrollbar settings.
     */
    for (r = rsfonts; r != (struct rsfont *) NULL; r = r->next) {
	Dimension cw, ch;	/* container_width, container_height */
	Dimension mkw;

	cw = SCREEN_WIDTH(r->width)+2 + scrollbar_width;
	mkw = min_keypad_width();
	if (kp_placement == kp_integral && xappres.keypad_on
		&& cw < mkw) {
	    cw = mkw;
	}

	ch = SCREEN_HEIGHT(r->height)+2 + menubar_qheight(cw);
	if (kp_placement == kp_integral && xappres.keypad_on) {
	    ch += keypad_qheight();
	}
	r->total_width = cw;
	r->total_height = ch;
	r->area = cw * ch;
    }

    /*
     * Find the the best match for requested dimensions.
     *
     * In past, the "best" was the closest area that was larger or
     * smaller than the current area, whichever was requested.
     *
     * Now, if they try to shrink the screen, "they" might be the
     * window manager enforcing a size restriction, so the "best"
     * match is the largest window that fits within the requested
     * dimensions.
     *
     * If they try to grow the screen, then the "best" is the
     * smallest font that lies between the current and requested
     * length in the requested dimension(s).
     *
     * An ambiguous request (one dimension larger and the other smaller)
     * is taken to be a "larger" request.
     */

    if ((cn.width <= main_width && cn.height <= main_height) ||
	(cn.width > main_width && cn.height > main_height)) {
	/*
	 * Shrink or two-dimensional grow: Find the largest font which
	 * fits within the new boundaries.
	 *
	 * Note that a shrink in one dimension, with the other
	 * dimension matching, is considered a two-dimensional
	 * shrink.
	 */
	for (r = rsfonts; r != (struct rsfont *) NULL; r = r->next) {
	    if (r->total_width <= cn.width && r->total_height <= cn.height) {
		if (best == NULL || r->area > best->area) {
		    best = r;
		}
	    }
	}
	/*
	 * If no font is small enough, see if there is a font smaller
	 * than the current font along the requested dimension(s),
	 * which is better than doing nothing.
	 */
	if (best == NULL) {
	    for (r = rsfonts; r != (struct rsfont *) NULL; r = r->next) {
		if (cn.width < main_width && r->total_width > main_width) {
		    continue;
		}
		if (cn.height < main_height && r->total_height > main_height) {
		    continue;
		}
		if (best == NULL || r->area < best->area) {
		    best = r;
		}
	    }
	}
    } else {
	/*
	 * One-dimensional grow: Find the largest font which fits
	 * within the lengthened boundary, and don't constrain the
	 * other dimension.
	 *
	 * Note than an ambiguous change (grow in one dimensional and
	 * shrink in the other) is considered a one-dimensional grow.
	 *
	 * In either case, the "other" dimension is considered
	 * unconstrained.
	 */
	if (cn.width > main_width) {
	    /* Wider. */
	    for (r = rsfonts; r != (struct rsfont *) NULL; r = r->next) {
		if (r->total_width <= cn.width &&
		    (best == NULL ||
		     r->total_width > best->total_width)) {
		    best = r;
		}
	    }
	} else {
	    /* Taller. */
	    for (r = rsfonts; r != (struct rsfont *) NULL; r = r->next) {
		if (r->total_height <= cn.height &&
			(best == NULL ||
			 r->total_height > best->total_height)) {
		    best = r;
		}
	    }
	}
    }

    /* Change fonts. */
    if (!best || (efontname && !strcmp(best->name, efontname))) {
	if (cn.width > main_width || cn.height > main_height) {
	    vtrace("  no larger font available\n"
		   "    reasserting previous size\n");
	} else {
	    vtrace("  no smaller font available\n"
		   "    reasserting previous size\n");
	}
	set_toplevel_sizes();
    } else {
	vtrace("    switching to font '%s', new size %dx%d\n",
		best->name, best->total_width, best->total_height);
	screen_newfont(best->name, false, false);

	/* screen_newfont() sets screen_redo to REDO_FONT. */
	screen_redo = REDO_RESIZE;
    }
}

static void
revert_screen(void)
{
    const char *revert = NULL;

    /* If there's a reconfiguration pending, try to undo it. */
    switch (screen_redo) {
    case REDO_FONT:
	revert = "font";
	screen_newfont(redo_old_font, false, false);
	break;
    case REDO_MODEL:
	revert = "model number";
	screen_change_model(redo_old_model,
	    redo_old_ov_cols, redo_old_ov_rows);
	break;
    case REDO_KEYPAD:
	revert = "keypad configuration";
	xappres.keypad_on = False;
	screen_showikeypad(xappres.keypad_on);
	break;
    case REDO_SCROLLBAR:
	revert = "scrollbar configuration";
	if (toggled(SCROLL_BAR)) {
	    toggle_toggle(SCROLL_BAR);
	    toggle_scrollBar(SCROLL_BAR, TT_INTERACTIVE);
	}
	break;
    case REDO_RESIZE:
	/* Changed fonts in response to a previous user resize. */
	vtrace("  size reassertion failed, window truncated\n"
		"  doing nothing\n");
	screen_redo = REDO_NONE;
	return;
    case REDO_NONE:
	/* Initial configuration, or user-generated resize. */
	do_resize();
	return;
    default:
	break;
    }

    /* Tell the user what we're doing. */
    if (revert != NULL) {
	vtrace("    reverting to previous %s\n", revert);
	popup_an_error("Main window does not fit on the X display\n"
		"Reverting to previous %s", revert);
    }

    screen_redo = REDO_NONE;
}

static void
revert_later(XtPointer closure _is_unused, XtIntervalId *id _is_unused)
{
	revert_screen();
}

/*
 * Timeout routine called 0.5 sec after x3270 receives the last ConfigureNotify
 * message.  This is for window managers that use 'continuous' move or resize
 * actions.
 */

static void
stream_end(XtPointer closure _is_unused, XtIntervalId *id _is_unused)
{
	bool needs_moving = false;

	vtrace("Stream timer expired %hux%hu+%hd+%hd\n",
		cn.width, cn.height, cn.x, cn.y);

	/* Not ticking any more. */
	cn.ticking = false;

	/* Save the new coordinates in globals for next time. */
	if (cn.x != main_x || cn.y != main_y) {
		main_x = cn.x;
		main_y = cn.y;
		needs_moving = true;
	}

	/*
	 * If the dimensions are correct, do nothing, forget about any
	 * reconfig we may need to revert, and get out.
	 */
	if (cn.width == main_width && cn.height == main_height) {
		vtrace("  width and height match\n"
			"  doing nothing\n");
		screen_redo = REDO_NONE;
		goto done;
	}

	/*
	 * If the dimensions are bigger, perhaps we've gotten some extra
	 * decoration.  Be persistent
	 */
	if (cn.width >= main_width && cn.height >= main_height) {
		vtrace("  bigger\n"
			"    asserting desired size\n");
		set_toplevel_sizes();
		screen_redo = REDO_NONE;
	}

	/* They're not correct. */
	vtrace("  size mismatch, want %ux%u", main_width, main_height);

	revert_screen();

    done:
	if (needs_moving && !iconic) {
		keypad_move();
		{
		    	static bool first = true;

			if (first)
			    	first = false;
			else
			    	popups_move();
		}
	}
}

void
PA_ConfigureNotify_xaction(Widget w _is_unused, XEvent *event,
	String *params _is_unused, Cardinal *num_params _is_unused)
{
    XConfigureEvent *re = (XConfigureEvent *) event;
    Position xx, yy;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_ConfigureNotify_xaction, event, params, num_params);
#endif /*]*/

    /*
     * Get the new window coordinates.  If the configure event reports it
     * as (0,0), ask for it explicitly.
     */
    if (re->x || re->y) {
	xx = re->x;
	yy = re->y;
    } else {
	XtVaGetValues(toplevel, XtNx, &xx, XtNy, &yy, NULL);
    }
    vtrace("ConfigureNotify %hux%hu+%hd+%hd\n", re->width, re->height, xx, yy);
    
    /* Save the latest values. */
    cn.x = xx;
    cn.y = yy;
    cn.width = re->width;
    cn.height = re->height;

    /* Set the stream timer for 0.5 sec from now. */
    if (cn.ticking) {
	XtRemoveTimeOut(cn.id);
    }
    cn.id = XtAppAddTimeOut(appcontext, 500, stream_end, 0);
    cn.ticking = true;
}

/*
 * Process a VisibilityNotify event, setting the 'visibile' flag in nss.
 * This will switch the behavior of screen scrolling.
 */
void
PA_VisibilityNotify_xaction(Widget w _is_unused, XEvent *event _is_unused,
	String *params _is_unused, Cardinal *num_params _is_unused)
{
    XVisibilityEvent *e;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_VisibilityNotify_xaction, event, params, num_params);
#endif /*]*/
    e = (XVisibilityEvent *)event;
    nss.obscured = (e->state != VisibilityUnobscured);
}

/*
 * Process a GraphicsExpose event, refreshing the screen if we have done
 * one or more failed XCopyArea calls.
 */
void
PA_GraphicsExpose_xaction(Widget w _is_unused, XEvent *event _is_unused,
	String *params _is_unused, Cardinal *num_params _is_unused)
{
    int i;

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_GraphicsExpose_xaction, event, params, num_params);
#endif /*]*/

    if (nss.copied) {
	/*
	 * Force a screen redraw.
	 */
	(void) memset((char *) ss->image, 0,
		(maxROWS*maxCOLS) * sizeof(union sp));
	if (visible_control) {
	    for (i = 0; i < maxROWS*maxCOLS; i++) {
		ss->image[i].bits.cc = EBC_space;
	    }
	}
	ctlr_changed(0, ROWS*COLS);
	cursor_changed = true;

	nss.copied = false;
    }
}

/* Display size functions. */
unsigned
display_width(void)
{
	return XDisplayWidth(display, default_screen);
}

unsigned
display_widthMM(void)
{
	return XDisplayWidthMM(display, default_screen);
}

unsigned
display_height(void)
{
	return XDisplayHeight(display, default_screen);
}

unsigned
display_heightMM(void)
{
	return XDisplayHeightMM(display, default_screen);
}

/* Translate an EBCDIC DBCS character to a display character. */
static void
xlate_dbcs(unsigned char c0, unsigned char c1, XChar2b *r)
{
	unsigned long u;
	int d;

	/* Translate NULLs to spaces. */
	if (c0 == EBC_null && c1 == EBC_null) {
		c0 = EBC_space;
		c1 = EBC_space;
	}
	/* Then handle special cases. */
	if ((c0 < 0x41 && (c0 != EBC_space && c1 != EBC_space)) || c0 == 0xff) {
		/* Junk. */
		r->byte1 = 0;
		r->byte2 = 0;
	}
	u = ebcdic_dbcs_to_unicode((c0 << 8) | c1, EUO_BLANK_UNDEF);
	d = display16_lookup(dbcs_font.d16_ix, u);
	if (d >= 0) {
		r->byte1 = (d >> 8) & 0xff;
		r->byte2 = d & 0xff;
	} else {
		r->byte1 = 0;
		r->byte2 = 0;
	}

#if defined(_ST) /*[*/
	printf("EBC %02x%02x -> X11 font %02x%02x\n",
		c0, c1, r->byte1, r->byte2);
#endif /*]*/
}

static void
destroy_callback_func(XIM current_ic, XPointer client_data, XPointer call_data)
{
	ic = NULL;
	im = NULL;
	ic_focus = 0;

#if defined(_ST) /*[*/
	printf("destroy_callback_func\n");
#endif /*]*/
}

#define OTS_LEN		(sizeof(PT_OVER_THE_SPOT) - 1)

static void
im_callback(Display *display, XPointer client_data, XPointer call_data)
{
	XIMStyles *xim_styles = NULL;
	XIMCallback destroy;
	int i, j;
	XVaNestedList preedit_attr = NULL;
	XPoint spot;
	XRectangle local_win_rect;
	static im_style_t im_styles[] = {
		{ XIMPreeditNothing  | XIMStatusNothing,    PT_ROOT },
		{ XIMPreeditPosition | XIMStatusNothing,    PT_OVER_THE_SPOT },
		{ XIMPreeditArea     | XIMStatusArea,       PT_OFF_THE_SPOT },
		{ XIMPreeditCallbacks| XIMStatusCallbacks,  PT_ON_THE_SPOT },
		{ (XIMStyle)0,                              NULL }
	};
	char *im_style = (xappres.preedit_type != NULL)?
	    strip_whitespace(xappres.preedit_type): PT_OVER_THE_SPOT;
	char c;

#if defined(_ST) /*[*/
	printf("im_callback\n");
#endif /*]*/

	if (!strcasecmp(im_style, "None"))
		return;

	/* Parse the offset value for OverTheSpot. */
	if (!strncasecmp(im_style, PT_OVER_THE_SPOT, OTS_LEN) &&
	    ((c = im_style[OTS_LEN]) == '+' ||
	     c == '-')) {
		ovs_offset = atoi(im_style + OTS_LEN);
		im_style = NewString(im_style);
		im_style[OTS_LEN] = '\0';
	}

	/* Open connection to IM server. */
	if ((im = XOpenIM(display, NULL, NULL, NULL)) == NULL) {
		popup_an_error("XOpenIM failed\nXIM-based input disabled");
		goto error_return;
	}

	destroy.callback = (XIMProc)destroy_callback_func;
	destroy.client_data = NULL;
	XSetIMValues(im, XNDestroyCallback, &destroy, NULL);

	/* Detect the input style supported by XIM server. */
	if (XGetIMValues(im, XNQueryInputStyle, &xim_styles, NULL) != NULL ||
			xim_styles == NULL) {
		popup_an_error("Input method doesn't support any styles\n"
			       "XIM-based input disabled");
		goto error_return;
	}
	for (i = 0; i < xim_styles->count_styles; i++) {
		for (j = 0; im_styles[j].description != NULL; j++) {
			if (im_styles[j].style ==
					xim_styles->supported_styles[i]) {
#if defined(_ST) /*[*/
				printf("XIM server supports input_style %s\n",
						im_styles[j].description);
#endif /*]*/
				break;
			}
		}
#if defined(_ST) /*[*/
		if (im_styles[j].description == NULL)
			printf("XIM server supports unknown input style %x\n",
				(unsigned)(xim_styles->supported_styles[i]));
#endif /*]*/
	}

	/* Set my preferred style. */
	for (j = 0; im_styles[j].description != NULL; j++) {
		if (!strcasecmp(im_styles[j].description, im_style)) {
			style = im_styles[j].style;
			break;
		}
	}
	if (im_styles[j].description == NULL) {
		popup_an_error("Input style '%s' not supported\n"
			       "XIM-based input disabled", im_style);
		goto error_return;
	}

	if (style == (XIMPreeditPosition | XIMStatusNothing)) {
		char *fsname;
		XFontSet fontset;
		char **charset_list;
		int charset_count;
		char *def_string;

		fsname = xs_buffer("-*-%s,-*-iso8859-1", efont_charset_dbcs);
		for (;;) {
#if defined(_ST) /*[*/
			printf("trying fsname: %s\n", fsname);
#endif /*]*/
			fontset = XCreateFontSet(display, fsname,
					&charset_list, &charset_count,
					&def_string);
			if (charset_count || fontset == NULL) {
				if (charset_count > 0) {
					int i;

					for (i = 0; i < charset_count; i++) {
#if defined(_ST) /*[*/
						printf("missing: %s\n",
							charset_list[0]);
#endif /*]*/
						fsname = xs_buffer("%s,-*-%s",
							fsname,
							charset_list[i]);
					}
					continue;

				}
				popup_an_error("Cannot create fontset '%s' "
					"for input context\n"
					"XIM-based input disabled",
					fsname);
				goto error_return;
			} else
				break;
		};

		spot.x = 0;
		spot.y = ovs_offset * nss.char_height;
		local_win_rect.x = 1;
		local_win_rect.y = 1;
		local_win_rect.width  = main_width;
		local_win_rect.height = main_height;
		preedit_attr = XVaCreateNestedList(0,
					XNArea, &local_win_rect,
					XNSpotLocation, &spot,
					XNFontSet, fontset,
					NULL);
	}

	/* Create IC. */
	ic = XCreateIC(im, XNInputStyle, style,
			XNClientWindow, nss.window,
			XNFocusWindow, nss.window,
			(preedit_attr) ? XNPreeditAttributes : NULL,
			preedit_attr,
			NULL);
	if (ic == NULL) {
		popup_an_error("Cannot create input context\n"
		               "XIM-based input disabled");
		goto error_return;
	}
	return;

    error_return:
	if (im != NULL) {
		XCloseIM(im);
		im = NULL;
		xim_error = true;
	}
}

static void
cleanup_xim(bool b _is_unused)
{
	if (ic != NULL)
		XDestroyIC(ic);
	if (im != NULL)
		XCloseIM(im);
}

static void
xim_init(void)
{
    char *buf = "";
    static bool xim_initted = false;
    char *s;

    if (!dbcs || xim_initted) {
	return;
    }

    xim_initted = true;

    s = setlocale(LC_CTYPE, "");
    if (s != NULL) {
	s = NewString(s);
    }
    Replace(locale_name, s);
    if (s == NULL) {
	popup_an_error("setlocale(LC_CTYPE) failed\nXIM-based input disabled");
	xim_error = true;
	return;
    }

    if (xappres.input_method != NULL) {
	buf = lazyaf("@im=%s", xappres.input_method);
    }
    if (XSetLocaleModifiers(buf) == NULL) {
	popup_an_error("XSetLocaleModifiers failed\nXIM-based input disabled");
	xim_error = true;
    } else if (XRegisterIMInstantiateCallback(display, NULL, NULL, NULL,
		im_callback, NULL) != true) {
	popup_an_error("XRegisterIMInstantiateCallback failed\n"
		"XIM-based input disabled");
	xim_error = true;
    }
    register_schange(ST_EXITING, cleanup_xim);
    return;
}

static void
send_spot_loc(void)
{
	XPoint spot;
	XVaNestedList preedit_attr;

	spot.x = (cursor_addr % COLS) * nss.char_width + hhalo;
	spot.y = ((cursor_addr / COLS) + ovs_offset) * nss.char_height + vhalo;
	preedit_attr = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
	XSetICValues(ic, XNPreeditAttributes, preedit_attr, NULL);
	XFree(preedit_attr);
}

/* Change the window title. */
static bool
Title_action(ia_t ia, unsigned argc, const char **argv)
{
    action_debug("Title", ia, argc, argv);
    if (check_argc("Title", argc, 1, 1) < 0) {
	return false;
    }

    user_title = NewString(argv[0]);
    XtVaSetValues(toplevel, XtNtitle, user_title, NULL);
    return true;
}

/* Change the window state. */
static bool
WindowState_action(ia_t ia, unsigned argc, const char **argv)
{
    int state;

    action_debug("WindowState", ia, argc, argv);
    if (check_argc("WindowState", argc, 1, 1) < 0) {
	return false;
    }

    if (!strcasecmp(argv[0], "Iconic")) {
	state = true;
    } else if (!strcasecmp(argv[0], "Normal")) {
	state = false;
    } else {
	popup_an_error("WindowState argument must be Iconic or Normal");
	return false;
    }
    XtVaSetValues(toplevel, XtNiconic, state, NULL);
    return true;
}

static dfc_t *dfc = NULL, *dfc_last = NULL;

/* Split a font name into parts. */
static int
split_name(const char *name, char res[15][256])
{
    	int ns;
	const char *dash;
	const char *s;

	ns = 0;
	s = name;
	while (ns < 14 && ((dash = strchr(s, '-')) != NULL)) {
		int nc = dash - s;

		if (nc >= 256)
			nc = 255;
		strncpy(res[ns], s, nc);
		res[ns][nc] = '\0';
		ns++;
		s = dash + 1;
	}
	if (*s) {
	    	int nc = strlen(s);

		if (nc >= 256)
		    	nc = 255;
		strncpy(res[ns], s, nc);
		res[ns][nc] = '\0';
		ns++;
	}

	return ns;
}

/* Initialize the dumb font cache. */
static void
dfc_init(void)
{
    	char **namelist;
	int count;
	int i;
	dfc_t *d, *e;
	char nl_arr[15][256];
	dfc_t *c_first = NULL;
	dfc_t *c_last = NULL;
	dfc_t *m_first = NULL;
	dfc_t *m_last = NULL;

	/* Get all of the font names. */
	namelist = XListFonts(display, "*", MAX_FONTS, &count);
	if (namelist == NULL)
	    	Error("No fonts");
	for (i = 0; i < count; i++) {
	    	/* Pick apart the font names. */
	    	int nf = split_name(namelist[i], nl_arr);
		int good = true;

		if ((nf == 1 && strncmp(nl_arr[0], "3270", 4)) ||
		    (nf != 15) ||
		    (strcasecmp(nl_arr[4], "r") ||
		     !strcmp(nl_arr[7], "0") ||
		     !strcmp(nl_arr[8], "0") ||
		     (strcasecmp(nl_arr[11], "c") &&
		      strcasecmp(nl_arr[11], "m")) ||
		     !strcmp(nl_arr[12], "0")))
			good = false;

		/* Make sure it isn't a dup. */
		for (e = dfc; e != NULL; e = e->next)
		    	if (!strcasecmp(namelist[i], e->name))
			    	break;
		if (e != NULL)
		    	continue;

	    	/* Append this entry to the cache. */
	    	d = (dfc_t *)Malloc(sizeof(dfc_t));
		d->next = NULL;
		d->name = NewString(namelist[i]);
		d->weight = NewString(nl_arr[3]);
		d->points = atoi(nl_arr[7]);
		d->spacing = NewString(nl_arr[11]);
		d->charset = xs_buffer("%s-%s", nl_arr[13], nl_arr[14]);
		d->good = good;
		if (!d->spacing[0] || !strcasecmp(d->spacing, "c")) {
			if (c_last)
				c_last->next = d;
			else
				c_first = d;
			c_last = d;
		} else {
			if (m_last)
				m_last->next = d;
			else
				m_first = d;
			m_last = d;
		}
	}

	if (c_first != NULL) {
	    	c_last->next = m_first;
		dfc = c_first;
		if (m_last != NULL)
		    	dfc_last = m_last;
		else
		    	dfc_last = c_last;
	} else {
	    	dfc = m_first;
		dfc_last = m_last;
	}
}

/* Search iteratively for fonts whose names specify a given character set. */
static const char *
dfc_search_family(const char *charset, dfc_t **dp, void **cookie)
{
    	dfc_t *d;

	if (*cookie == NULL)
	    	d = dfc;
	else {
	    	d = ((dfc_t *)*cookie)->next;
		if (d == NULL) {
		    	if (dp)
			    	*dp = NULL;
		    	*cookie = NULL;
		    	return NULL;
		}
	}
	while (d != NULL) {
	    	if (d->good && !strcasecmp(charset, d->charset)) {
		    	if (dp)
			    	*dp = d;
			*cookie = d;
			return d->name;
		}
		d = d->next;
	}
	*cookie = NULL;
	return NULL;
}

/* Search for a font by name. */
static bool
dfc_search_name(const char *name)
{
    	dfc_t *d;

	for (d = dfc; d != NULL; d = d->next)
	    	if (!strcasecmp(name, d->name))
		    	return true;

	return false;
}

/* Return the window for the screen. */
unsigned long
screen_window_number(void)
{
    return XtWindow(toplevel);
}

/**
 * External interface to the SELECTED macro.
 *
 * @param[in] baddr	Buffer address.
 *
 * @return true if cell is selected
 */
bool
screen_selected(int baddr)
{
    return SELECTED(baddr) != 0;
}

/**
 * External interface to the SET_SELECT macro.
 *
 * @param[in] baddr	Buffer address.
 */
void
screen_set_select(int baddr)
{
    SET_SELECT(baddr);
}

/**
 * Unselect everything.
 */
void
screen_unselect_all(void)
{
    (void) memset((char *)selected, 0, (ROWS*COLS + 7) / 8);
}

/**
 * Does this display support background color? (No.)
 *
 * @return true if supported, false if not.
 */
bool
screen_has_bg_color(void)
{
    return false;
}

/**
 * Screen module registration.
 */
void
screen_register(void)
{
    static toggle_register_t toggles[] = {
	{ MONOCASE,		toggle_monocase,	0 },
	{ ALT_CURSOR,		toggle_altCursor,	0 },
	{ CURSOR_BLINK,		toggle_cursorBlink,	0 },
	{ SHOW_TIMING,		toggle_showTiming,	0 },
	{ CURSOR_POS,		toggle_cursorPos,	0 },
	{ CROSSHAIR,		toggle_crosshair,	0 },
	{ VISIBLE_CONTROL,	toggle_visible_control, 0 },
	{ SCROLL_BAR,		toggle_scrollBar,	0 },
	{ MARGINED_PASTE,	NULL,			0 },
	{ OVERLAY_PASTE,	NULL,			0 }
    };
    static action_table_t screen_actions[] = {
	{ "SetFont",		SetFont_action,		ACTION_KE },
	{ "Title",		Title_action,		ACTION_KE },
	{ "WindowState",	WindowState_action,	ACTION_KE }
    };

    /* Register our toggles. */
    register_toggles(toggles, array_count(toggles));

    /* Register our actions. */
    register_actions(screen_actions, array_count(screen_actions));

    /* Register state change callbacks. */
    register_schange(ST_HALF_CONNECT, screen_connect);
    register_schange(ST_CONNECT, screen_connect);
    register_schange(ST_3270_MODE, screen_connect);
}
