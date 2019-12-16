/*
 * Copyright (c) 2019 Paul Mattes.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes nor the names of his contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *      find_terminal.c
 *              Console window support.
 */

#if defined(_WIN32) /*[*/
# error Not for Windows
#endif /*]*/

#include "globals.h"

#include "appres.h"
#include "find_terminal.h"
#include "lazya.h"

/* Well-known consoles, in order of preference. */
static terminal_desc_t terminals[] = {
    { "gnome-terminal", "--title", NULL, "--" },
    { "konsole", "--caption", NULL, "-e" },
    { "xfce4-terminal", "-T", NULL, "-x" },
    { "xterm", "-title", "-sb", "-e" },
    { NULL, NULL, NULL, NULL }
};

/* Find an executable in $PATH. */
bool
find_in_path(const char *program)
{
    char *path = getenv("PATH");
    char *colon;

    while ((colon = strchr(path, ':')) != NULL) {
	if (colon != path) {
	    char *xpath = lazyaf("%.*s/%s", (int)(colon - path), path, program);

	    if (access(xpath, X_OK) == 0) {
		return true;
	    }
	}
	path = colon + 1;
    }
    if (*path) {
	char *xpath = lazyaf("%s/%s", path, program);

	if (access(xpath, X_OK) == 0) {
	    return true;
	}
    }
    return false;
}

/* Find the preferred terminal emulator for the prompt. */
terminal_desc_t *
find_terminal(void)
{
    int i;

    do {
	static terminal_desc_t t_ret;
	char *override = appres.interactive.console;
	char *program, *title_opt, *extra_opt, *exec_opt;

	/*
	 * The format is:
	 *   title_opt:title-opt:extra-opt:exec-opt.
	 **/
	if (override == NULL) {
	    break;
	}
	program = lazya(NewString(override));
	title_opt = strchr(program, ':');
	if (title_opt != NULL) {
	    *(title_opt++) = '\0';
	} else {
	    if (*program == '\0') {
		break;
	    }

	    /* They just specified the name. */
	    for (i = 0; terminals[i].program != NULL; i++) {
		if (!strcmp(program, terminals[i].program) &&
			find_in_path(program)) {
		    return &terminals[i];
		}
	    }
	    break;
	}

	extra_opt = strchr(title_opt, ':');
	if (extra_opt != NULL) {
	    *(extra_opt++) = '\0';
	} else {
	    break;
	}

	exec_opt = strchr(extra_opt, ':');
	if (exec_opt != NULL) {
	    *(exec_opt++) = '\0';
	} else {
	    break;
	}

	if (*program == '\0' || *title_opt == '\0' || *exec_opt == '\0') {
	    break;
	}

	t_ret.program = program;
	t_ret.title_opt = title_opt;
	if (*extra_opt != '\0') {
	    t_ret.extra_opt = extra_opt;
	} else {
	    t_ret.extra_opt = NULL;
	}
	t_ret.exec_opt = exec_opt;
	if (find_in_path(t_ret.program)) {
	    return &t_ret;
	}
    } while (false);

    /* No override. Find the best one. */
    for (i = 0; terminals[i].program != NULL; i++) {
	if (find_in_path(terminals[i].program)) {
	    return &terminals[i];
	}
    }
    return NULL;
}
