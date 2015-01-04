/* Copyright (c) 1993-2009, 2013-2015 Paul Mattes.
 * Copyright (c) 1990, Jeff Sparkes.
 * Copyright (c) 1989, Georgia Tech Research Corporation (GTRC), Atlanta, GA
 *  30332.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes, Jeff Sparkes, GTRC nor the names of
 *       their contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES, JEFF SPARKES AND GTRC "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PAUL MATTES, JEFF SPARKES OR GTRC BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	xkybd.c
 *		Xt-specific keyboard functions.
 */

#include "globals.h"

#include <X11/Xatom.h>
#define XK_3270
#if defined(X3270_APL) /*[*/
# define XK_APL
#endif /*]*/
#include <X11/keysym.h>

#include <fcntl.h>
#include "3270ds.h"
#include "appres.h"
#include "ctlr.h"
#include "keysym2ucs.h"
#include "resources.h"
#include "screen.h"

#include "actionsc.h"
#include "aplc.h"
#include "charsetc.h"
#include "ctlrc.h"
#include "unicodec.h"
#include "ftc.h"
#include "hostc.h"
#include "idlec.h"
#include "keymapc.h"
#include "keypadc.h"
#include "kybdc.h"
#include "linemodec.h"
#include "macrosc.h"
#include "nvtc.h"
#include "popupsc.h"
#include "print_screen.h"
#include "screenc.h"
#include "scrollc.h"
#include "selectc.h"
#include "statusc.h"
#include "tablesc.h"
#include "telnetc.h"
#include "togglesc.h"
#include "trace.h"
#include "utf8c.h"
#include "utilc.h"
#include "xactionsc.h"
#include "xkybdc.h"


/*
 * Handle an ordinary character key, given its NULL-terminated multibyte
 * representation.
 */
static void
key_ACharacter(char *mb, enum keytype keytype, enum iaction cause)
{
	ucs4_t ucs4;
	int consumed;
	enum me_fail error;

	reset_idle_timer();

	/* Convert the multibyte string to UCS4. */
	ucs4 = multibyte_to_unicode(mb, strlen(mb), &consumed, &error);
	if (ucs4 == 0) {
		vtrace(" %s -> Key(?)\n", ia_name[(int) cause]);
		vtrace("  dropped (invalid multibyte sequence)\n");
		return;
	}

	key_UCharacter(ucs4, keytype, cause);
}

static Boolean
AltCursor_action(ia_t ia, unsigned argc, const char **argv)
{
    action_debug("AltCursor", ia, argc, argv);
    if (check_argc("AltCursor", argc, 0, 0) < 0) {
	return False;
    }
    reset_idle_timer();
    do_toggle(ALT_CURSOR);
    return True;
}

/*
 * Cursor Select mouse action (light pen simulator).
 */
void
MouseSelect_xaction(Widget w, XEvent *event, String *params,
	Cardinal *num_params)
{
    xaction_debug(MouseSelect_xaction, event, params, num_params);
    if (xcheck_usage(MouseSelect_xaction, *num_params, 0, 0) < 0) {
	return;
    }
    if (w != *screen) {
	return;
    }
    reset_idle_timer();
    if (kybdlock) {
	return;
    }
    if (IN_NVT) {
	return;
    }
    lightpen_select(mouse_baddr(w, event));
}

/*
 * MoveCursor Xt action.  Depending on arguments, this is either a move to the
 * mouse cursor position, or to an absolute location.
 */
void
MoveCursor_xaction(Widget w, XEvent *event, String *params,
	Cardinal *num_params)
{
    xaction_debug(MoveCursor_xaction, event, params, num_params);

    /* With arguments, this isn't a mouse call. */
    if (*num_params != 0) {
	if (*num_params != 2) {
	    popup_an_error("MoveCursor takes 0 or 2 arguments");
	    return;
	}
	run_action("MoveCursor", IA_KEYMAP, params[0], params[1]);
	return;
    }

    /* If it is a mouse call, it only applies to the screen. */
    if (w != *screen) {
	return;
    }

    /* If the screen is locked, do nothing. */
    if (kybdlock) {
	return;
    }

    /* Move the cursor to where the mouse is. */
    reset_idle_timer();
    cursor_move(mouse_baddr(w, event));
}


/*
 * Run a KeyPress through XIM.
 * Returns True if there is further processing to do, False otherwise.
 */
static Boolean
xim_lookup(XKeyEvent *event)
{
	static char *buf = NULL;
	static int buf_len = 0, rlen;
	KeySym k;
	Status status;
	int i;
	Boolean rv = False;
#define BASE_BUFSIZE 50

	if (ic == NULL)
		return True;

	if (buf == NULL) {
		buf_len = BASE_BUFSIZE;
		buf = Malloc(buf_len);
	}

	for (;;) {
		memset(buf, '\0', buf_len);
		rlen = XmbLookupString(ic, event, buf, buf_len - 1, &k,
					&status);
		if (status != XBufferOverflow)
			break;
		buf_len += BASE_BUFSIZE;
		buf = Realloc(buf, buf_len);
	}

	switch (status) {
	case XLookupNone:
		rv = False;
		break;
	case XLookupKeySym:
		rv = True;
		break;
	case XLookupChars:
		vtrace("%d XIM char%s:", rlen, (rlen != 1)? "s": "");
		for (i = 0; i < rlen; i++) {
			vtrace(" %02x", buf[i] & 0xff);
		}
		vtrace("\n");
		buf[rlen] = '\0';
		key_ACharacter(buf, KT_STD, ia_cause);
		rv = False;
		break;
	case XLookupBoth:
		rv = True;
		break;
	}
	return rv;
}

void
ignore_xaction(Widget w _is_unused, XEvent *event, String *params,
	Cardinal *num_params)
{
    xaction_debug(ignore_xaction, event, params, num_params);
    reset_idle_timer();
}

/*
 * X-dependent code starts here.
 */

/*
 * Translate a keymap (from an XQueryKeymap or a KeymapNotify event) into
 * a bitmap of Shift, Meta or Alt keys pressed.
 */
#define key_is_down(kc, bitmap) (kc && ((bitmap)[(kc)/8] & (1<<((kc)%8))))
int
state_from_keymap(char keymap[32])
{
	static Boolean	initted = False;
	static KeyCode	kc_Shift_L, kc_Shift_R;
	static KeyCode	kc_Meta_L, kc_Meta_R;
	static KeyCode	kc_Alt_L, kc_Alt_R;
	int	pseudo_state = 0;

	if (!initted) {
		kc_Shift_L = XKeysymToKeycode(display, XK_Shift_L);
		kc_Shift_R = XKeysymToKeycode(display, XK_Shift_R);
		kc_Meta_L  = XKeysymToKeycode(display, XK_Meta_L);
		kc_Meta_R  = XKeysymToKeycode(display, XK_Meta_R);
		kc_Alt_L   = XKeysymToKeycode(display, XK_Alt_L);
		kc_Alt_R   = XKeysymToKeycode(display, XK_Alt_R);
		initted = True;
	}
	if (key_is_down(kc_Shift_L, keymap) ||
	    key_is_down(kc_Shift_R, keymap))
		pseudo_state |= ShiftKeyDown;
	if (key_is_down(kc_Meta_L, keymap) ||
	    key_is_down(kc_Meta_R, keymap))
		pseudo_state |= MetaKeyDown;
	if (key_is_down(kc_Alt_L, keymap) ||
	    key_is_down(kc_Alt_R, keymap))
		pseudo_state |= AltKeyDown;
	return pseudo_state;
}
#undef key_is_down

/*
 * Process shift keyboard events.  The code has to look for the raw Shift keys,
 * rather than using the handy "state" field in the event structure.  This is
 * because the event state is the state _before_ the key was pressed or
 * released.  This isn't enough information to distinguish between "left
 * shift released" and "left shift released, right shift still held down"
 * events, for example.
 *
 * This function is also called as part of Focus event processing.
 */
void
PA_Shift_xaction(Widget w _is_unused, XEvent *event _is_unused,
	String *params _is_unused, Cardinal *num_params _is_unused)
{
    char keys[32];

#if defined(INTERNAL_ACTION_DEBUG) /*[*/
    xaction_debug(PA_Shift_xaction, event, params, num_params);
#endif /*]*/
    XQueryKeymap(display, keys);
    shift_event(state_from_keymap(keys));
}

/*
 * Called by the toolkit for any key without special actions.
 */
void
Default_xaction(Widget w _is_unused, XEvent *event, String *params,
	Cardinal *num_params)
{
    XKeyEvent	*kevent = (XKeyEvent *)event;
    char	buf[32];
    KeySym	ks;
    int		ll;

    xaction_debug(Default_xaction, event, params, num_params);
    if (xcheck_usage(Default_xaction, *num_params, 0, 0) < 0) {
	return;
    }
    switch (event->type) {
    case KeyPress:
	if (!xim_lookup((XKeyEvent *)event)) {
	    return;
	}
	ll = XLookupString(kevent, buf, 32, &ks, NULL);
	buf[ll] = '\0';
	if (ll > 1) {
	    key_ACharacter(buf, KT_STD, IA_DEFAULT);
	    return;
	}
	if (ll == 1) {
	    /* Remap certain control characters. */
	    if (!IN_NVT) switch (buf[0]) {
		case '\t':
		    run_action("Tab", IA_DEFAULT, NULL, NULL);
		    break;
	       case '\177':
		    run_action("Delete", IA_DEFAULT, NULL, NULL);
		    break;
		case '\b':
		    run_action("Erase", IA_DEFAULT, NULL, NULL);
		    break;
		case '\r':
		    run_action("Enter", IA_DEFAULT, NULL, NULL);
		    break;
		case '\n':
		    run_action("Newline", IA_DEFAULT, NULL, NULL);
		    break;
		default:
		    key_ACharacter(buf, KT_STD, IA_DEFAULT);
		    break;
	    } else {
		key_ACharacter(buf, KT_STD, IA_DEFAULT);
	    }
	    return;
	}

	/* Pick some other reasonable defaults. */
	switch (ks) {
	case XK_Up:
	    run_action("Up", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Down:
	    run_action("Down", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Left:
	    run_action("Left", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Right:
	    run_action("Right", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Insert:
#if defined(XK_KP_Insert) /*[*/
	case XK_KP_Insert:
#endif /*]*/
	    run_action("Insert", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Delete:
	    run_action("Delete", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Home:
	    run_action("Home", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Tab:
	    run_action("Tab", IA_DEFAULT, NULL, NULL);
	    break;
#if defined(XK_ISO_Left_Tab) /*[*/
	case XK_ISO_Left_Tab:
	    run_action("BackTab", IA_DEFAULT, NULL, NULL);
	    break;
#endif /*]*/
	case XK_Clear:
	    run_action("Clear", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_Sys_Req:
	    run_action("SysReq", IA_DEFAULT, NULL, NULL);
	    break;
#if defined(XK_EuroSign) /*[*/
	case XK_EuroSign:
	    run_action("Key", IA_DEFAULT, "currency", NULL);
	    break;
#endif /*]*/

#if defined(XK_3270_Duplicate) /*[*/
	/* Funky 3270 keysyms. */
	case XK_3270_Duplicate:
	    run_action("Dup", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_FieldMark:
	    run_action("FieldMark", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_Right2:
	    run_action("Right2", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_Left2:
	    run_action("Left2", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_BackTab:
	    run_action("BackTab", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_EraseEOF:
	    run_action("EraseEOF", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_EraseInput:
	    run_action("EraseInput", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_Reset:
	    run_action("Reset", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_PA1:
	    run_action("PA", IA_DEFAULT, "1", NULL);
	    break;
	case XK_3270_PA2:
	    run_action("PA", IA_DEFAULT, "2", NULL);
	    break;
	case XK_3270_PA3:
	    run_action("PA", IA_DEFAULT, "3", NULL);
	    break;
	case XK_3270_Attn:
	    run_action("Attn", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_AltCursor:
	    run_action("AltCursor", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_CursorSelect:
	    run_action("CursorSelect", IA_DEFAULT, NULL, NULL);
	    break;
	case XK_3270_Enter:
	    run_action("Enter", IA_DEFAULT, NULL, NULL);
	    break;
#endif /*]*/

#if defined(X3270_APL) /*[*/
	/* Funky APL keysyms. */
	case XK_downcaret:
	    run_action("Key", IA_DEFAULT, "apl_downcaret", NULL);
	    break;
	case XK_upcaret:
	    run_action("Key", IA_DEFAULT, "apl_upcaret", NULL);
	    break;
	case XK_overbar:
	    run_action("Key", IA_DEFAULT, "apl_overbar", NULL);
	    break;
	case XK_downtack:
	    run_action("Key", IA_DEFAULT, "apl_downtack", NULL);
	    break;
	case XK_upshoe:
	    run_action("Key", IA_DEFAULT, "apl_upshoe", NULL);
	    break;
	case XK_downstile:
	    run_action("Key", IA_DEFAULT, "apl_downstile", NULL);
	    break;
	case XK_underbar:
	    run_action("Key", IA_DEFAULT, "apl_underbar", NULL);
	    break;
	case XK_jot:
	    run_action("Key", IA_DEFAULT, "apl_jot", NULL);
	    break;
	case XK_quad:
	    run_action("Key", IA_DEFAULT, "apl_quad", NULL);
	    break;
	case XK_uptack:
	    run_action("Key", IA_DEFAULT, "apl_uptack", NULL);
	    break;
	case XK_circle:
	    run_action("Key", IA_DEFAULT, "apl_circle", NULL);
	    break;
	case XK_upstile:
	    run_action("Key", IA_DEFAULT, "apl_upstile", NULL);
	    break;
	case XK_downshoe:
	    run_action("Key", IA_DEFAULT, "apl_downshoe", NULL);
	    break;
	case XK_rightshoe:
	    run_action("Key", IA_DEFAULT, "apl_rightshoe", NULL);
	    break;
	case XK_leftshoe:
	    run_action("Key", IA_DEFAULT, "apl_leftshoe", NULL);
	    break;
	case XK_lefttack:
	    run_action("Key", IA_DEFAULT, "apl_lefttack", NULL);
	    break;
	case XK_righttack:
	    run_action("Key", IA_DEFAULT, "apl_righttack", NULL);
	    break;
#endif /*]*/

	default:
	    if (ks >= XK_F1 && ks <= XK_F24) {
		(void) snprintf(buf, sizeof(buf), "%ld", ks - XK_F1 + 1);
		run_action("PF", IA_DEFAULT, buf, NULL);
	    } else {
		ucs4_t ucs4;

		ucs4 = keysym2ucs(ks);
		if (ucs4 != (ucs4_t)-1) {
		    key_UCharacter(ucs4, KT_STD, IA_KEY);
		} else {
		    vtrace(" Default: dropped (unknown keysym)\n");
		}
	    }
	    break;
	}
	break;

    case ButtonPress:
    case ButtonRelease:
	vtrace(" Default: dropped (no action configured)\n");
	break;
    default:
	vtrace(" Default: dropped (unknown event type)\n");
	break;
    }
}

/*
 * Set or clear a temporary keymap.
 *
 *   TemporaryKeymap(x)		toggle keymap "x" (add "x" to the keymap, or if
 *				"x" was already added, remove it)
 *   TemporaryKeymap()		removes the previous keymap, if any
 *   TemporaryKeymap(None)	removes the previous keymap, if any
 */
Boolean
TemporaryKeymap_action(ia_t ia, unsigned argc, const char **argv)
{
    action_debug("TemporaryKeymap", ia, argc, argv);
    if (check_argc("TemporaryKeymap", argc, 0, 1) < 0) {
	return False;
    }

    reset_idle_timer();

    if (argc == 0 || !strcmp(argv[0], "None")) {
	(void) temporary_keymap(NULL);
	return True;
    }

    if (temporary_keymap(argv[0]) < 0) {
	popup_an_error("TemporaryKeymap: Can't find %s %s", ResKeymap,
		argv[0]);
	cancel_if_idle_command();
	return False;
    }
    return True;
}

/* Initialize X11-specific keyboard functions. */
void
xkybd_init(void)
{
    static action_table_t xkybd_actions[] = {
	{ "AltCursor",		AltCursor_action,	ACTION_KE },
	{ "Keymap",		TemporaryKeymap_action,	ACTION_KE },
	{ "TemporaryKeymap",	TemporaryKeymap_action,	ACTION_KE }
    };

    register_actions(xkybd_actions, array_count(xkybd_actions));
}