/*
 * Copyright (c) 2014-2016, 2018-2021 Paul Mattes.
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
 *      httpd-json.c
 *              x3270 webserver, JSON module
 */

#include "globals.h"

#if !defined(_WIN32) /*[*/
# include <unistd.h>
# include <netinet/in.h>
# include <sys/select.h>
# include <arpa/inet.h>
#endif /*]*/
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

#include "appres.h"
#include "bind-opt.h"
#include "json.h"
#include "lazya.h"
#include "popups.h"
#include "resources.h"
#include "task.h"
#include "toggles.h"
#include "trace.h"
#include "utils.h"
#include "varbuf.h"

#include "httpd-core.h"
#include "httpd-io.h"
#include "httpd-json.h"
#include "httpd-nodes.h"

#if defined(_WIN32) /*[*/
# include "w3misc.h"
# include "winprint.h"
#endif /*]*/

/**
 * Compare two fixed-size strings
 *
 * @param[in] key	Non-NULL-terminated key
 * @param[in] key_length Length of key
 * @param[in] match_key	NULL-terminated key to match
 *
 * @return True if they match
 */
static bool
json_key_matches(const char *key, size_t key_length, const char *match_key)
{
    size_t sl = strlen(match_key);

    return key_length == sl && !strncmp(key, match_key, sl);
}

/**
 * Free a vector of commands.
 *
 * @param[in,out] cmds	Commands to free
 *
 * @return NULL
 */
static cmd_t **
free_cmds(cmd_t **cmds)
{

    if (cmds != NULL) {
	int cmd_ix;
	cmd_t *c;

	for (cmd_ix = 0; (c = cmds[cmd_ix]) != NULL; cmd_ix++) {
	    int arg_ix;

	    Free((char *)c->action);
	    c->action = NULL;
	    for (arg_ix = 0; c->args[arg_ix] != NULL; arg_ix++) {
		Free((char *)c->args[arg_ix]);
	    }
	}
	Free(cmds);
    }
    return NULL;
}

/**
 * Parse a JSON-formatted command.
 *
 * @param[in] json	Parsed JSON object
 * @param[out] cmd	Parsed action and arguments
 * @param[out] errmsg	Error message if parsing fails
 *
 * @return True for success
 */
static bool
hjson_parse_one(json_t *json, cmd_t **cmd, char **errmsg)
{
    char *action = NULL;
    json_t *jaction = NULL;
    const json_t *element;
    const char *key;
    size_t key_length;
    unsigned i;
    const char *value;
    size_t len;
    char **args = NULL;

    *errmsg = NULL;

    /*
     * It needs to be a structure with one or two fields: action (a string) and
     * optional args (an array of scalar types).
     */
    if (json_type(json) != JT_STRUCT) {
	*errmsg = NewString("Not a struct");
	goto fail;
    }

    /* Find the action. */
    if (!json_struct_member(json, "action", NT, &jaction)) {
	*errmsg = NewString("Missing struct element 'action'");
	goto fail;
    }
    if (json_type(jaction) != JT_STRING) {
	*errmsg = NewString("Invalid 'action' type");
	goto fail;
    }
    value = json_string_value(jaction, &len);
    action = xs_buffer("%.*s", (int)len, value);

    BEGIN_JSON_STRUCT_FOREACH(json, key, key_length, element) {
	if (json_key_matches(key, key_length, "action")) {
	    continue;
	}
	if (json_key_matches(key, key_length, "args")) {
	    unsigned array_length;

	    if (json_type(element) != JT_ARRAY) {
		*errmsg = NewString("Invalid 'args' type");
		goto fail;
	    }
	    array_length = json_array_length(element);
	    for (i = 0; i < array_length; i++) {
		const json_t *arg = json_array_element(element, i);

		switch (json_type(arg)) {
		case JT_NULL:
		case JT_BOOLEAN:
		case JT_INTEGER:
		case JT_DOUBLE:
		case JT_STRING:
		    break;
		default:
		    *errmsg = NewString("Invalid arg type");
		    goto fail;
		}
	    }
	    args = (char **)Calloc(array_length + 1, sizeof(char *));
	    for (i = 0; i < array_length; i++) {
		const json_t *arg = json_array_element(element, i);

		switch (json_type(arg)) {
		case JT_NULL:
		    args[i] = NewString("");
		    break;
		case JT_BOOLEAN:
		    args[i] = xs_buffer("%s",
			    json_boolean_value(arg)? "true": "false");
		    break;
		case JT_INTEGER:
		    args[i] = xs_buffer("%"PRId64, json_integer_value(arg));
		    break;
		case JT_DOUBLE:
		    args[i] = xs_buffer("%g", json_double_value(arg));
		    break;
		case JT_STRING:
		    value = json_string_value(arg, &len);
		    args[i] = xs_buffer("%.*s", (int)len, value);
		    break;
		default:
		    break;
		}
	    }
	    continue;
	}
	*errmsg = xs_buffer("Unknown struct element '%.*s'", (int)key_length,
		key);
	goto fail;
    } END_JSON_STRUCT_FOREACH(j, key, key_length, element);

    /* Default the arguments if necessary. */
    if (args == NULL) {
	args = (char **)Malloc(sizeof(char *));
	args[0] = NULL;
    }

    /* Return the command. */
    *cmd = (cmd_t *)Calloc(1, sizeof(cmd_t *));
    (*cmd)->action = action;
    (*cmd)->args = (const char **)args;
    return true;

fail:
    if (action != NULL) {
	Replace(action, NULL);
    }
    if (args != NULL) {
	for (i = 0; args[i] != NULL; i++) {
	    Replace(args[i], NULL);
	}
	Replace(args, NULL);
    }
    return false;
}

/**
 * Parse a JSON-formatted command or a set of commands.
 *
 * @param[in] cmd	Command to parse, in JSON format
 * @param[in] cmd_len	Length of command
 * @param[out] cmds	Parsed actions and arguments
 * @param[out] errmsg	Error message if parsing fails
 *
 * @return True for success
 */
bool
hjson_parse(const char *cmd, size_t cmd_len, cmd_t ***cmds, char **errmsg)
{
    json_t *json;
    json_errcode_t errcode;
    json_parse_error_t *error;
    unsigned array_length;
    unsigned i;
    cmd_t **c = NULL;

    *cmds = NULL;
    *errmsg = NULL;

    /* Parse the JSON. */
    errcode = json_parse(cmd, cmd_len, &json, &error);
    if (errcode != JE_OK) {
	*errmsg = xs_buffer("JSON parse error: line %d, column %d: %s",
		error->line, error->column, error->errmsg);
	goto fail;
    }

    /* The object can either be a struct or an array of structs. */
    switch (json_type(json)) {
    case JT_ARRAY:
	array_length = json_array_length(json);
	break;
    case JT_STRUCT:
	array_length = 1;
	break;
    default:
	*errmsg = NewString("Not a struct or array");
	goto fail;
    }

    /* Allocate the vector. */
    c = (cmd_t **)Calloc(array_length + 1, sizeof(cmd_t *));

    switch (json_type(json)) {
    case JT_ARRAY:
	for (i = 0; i < array_length; i++) {
	    char *elt_error;

	    if (!hjson_parse_one(json_array_element(json, i), &c[i],
			&elt_error)) {
		*errmsg = xs_buffer("Element %u: %s", i, elt_error);
		Free(elt_error);
		goto fail;
	    }
	}
	break;
    case JT_STRUCT:
	if (!hjson_parse_one(json, &c[0], errmsg)) {
	    goto fail;
	}
	break;
    default:
	break;
    }

    /* Success. */
    json_free_both(json, error);
    *cmds = c;
    return true;

fail:
    json_free_both(json, error);
    c = free_cmds(c);
    return false;
}