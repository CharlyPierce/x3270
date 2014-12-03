/*
 * Copyright (c) 2014, Paul Mattes.
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
 *      bind-opt.c
 *              Option parsing for -scriptport and -httpd.
 */

#include "globals.h"

#include <limits.h>
#include <errno.h>
#if !defined(_WIN32) /*[*/
# include <netinet/in.h>
#endif /*]*/

#include "resolverc.h"

#include "bind-optc.h"

typedef union {
    struct sockaddr_in v4;
#if defined(X3270_IPV6) /*[*/
    struct sockaddr_in6 v6;
#endif /*]*/
} sau_t;

/**
 * Return an IPv4 address.
 */
static Boolean
construct_v4(in_addr_t addr, unsigned short port, struct sockaddr **sa,
	socklen_t *addrlen)
{
    struct sockaddr_in *sin;

    sin = Malloc(sizeof(*sin));
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(addr);
    sin->sin_port = htons(port);
    *sa = (struct sockaddr *)sin;
    *addrlen = sizeof(*sin);
    return True;
}

/**
 * Parse a bind option for -httpd or -scriptport.
 *
 * Syntax:
 *  <port>
 *   implies 127.0.0.1
 *  <ip4addr>:<port>
 *  *:<port>
 *   implies 0.0.0.0
 *  [<ip6addr>]:<port>
 *
 * So, to bind to INADDR_ANY, port 4080, specify:
 *  *:4080
 * or
 *  0.0.0.0:4080
 * To bind to the same thing in IPv6, specify:
 *  [::]:4080
 * To bind to the IPv6 loopback address, specify:
 *  [::1]:4080
 *
 * It does not understand symbolic port names like 'telnet', and it does not
 * understand symbolic host names.
 *
 * @param[in] spec	string to parse
 * @param[out] addr	returned address
 * @param[out] addrlen	returned length of address
 *
 * @return True if address parsed successfully, False otherwise
 */
Boolean
parse_bind_opt(const char *spec, struct sockaddr **addr, socklen_t *addrlen)
{
    unsigned long u;
    char *end;
    size_t hlen;
    char *host_str;
    char *port_str;
    unsigned short port;
    char errmsg[256];
    rhp_t rv;

    /* Start with nothing. */
    *addr = NULL;
    *addrlen = 0;

    /* Try for the easiest case: a number. */
    u = strtoul(spec, &end, 10);
    if (u != 0 && u <= USHRT_MAX && *end == '\0') {
	return construct_v4(INADDR_LOOPBACK, (unsigned short)u, addr, addrlen);
    }

    /* Try for another easy one: *:<port> */
    if (!strncmp(spec, "*:", 2)) {
	u = strtoul(spec + 2, &end, 10);
	if (u != 0 && u <= USHRT_MAX && *end == '\0') {
	    return construct_v4(INADDR_ANY, (unsigned short)u, addr, addrlen);
	} else {
	    return False;
	}
    }

    /* Tease apart the syntax. */
    if (spec[0] == '[') {
	char *rbrack = strchr(spec, ']');

	/* We appear to have a hostname in square brackets. */
	if (rbrack == NULL ||
	    rbrack == spec + 1 ||
	    *(rbrack + 1) != ':' ||
	    !*(rbrack + 2)) {

	    return False;
	}

	hlen = rbrack - spec - 1;
	host_str = Malloc(hlen + 1);
	strncpy(host_str, spec + 1, hlen);
	host_str[hlen] = '\0';

	port_str = Malloc(strlen(rbrack + 2) + 1);
	strcpy(port_str, rbrack + 2);
    } else {
	char *colon = strchr(spec, ':');

	/* We appear to have a <host>:<port>. */
	if (colon == NULL || colon == spec || !*(colon + 1)) {
	    return False;
	}

	hlen = colon - spec;
	host_str = Malloc(hlen + 1);
	strncpy(host_str, spec, hlen);
	host_str[hlen] = '\0';

	port_str = Malloc(strlen(colon + 1) + 1);
	strcpy(port_str, colon + 1);
    }

    /* Use the resolver to figure out the mess. */
    *addr = Malloc(sizeof(sau_t));
    rv = resolve_host_and_port(host_str, port_str, 0, &port, *addr, addrlen,
	    errmsg, sizeof(errmsg), NULL);
    Free(host_str);
    Free(port_str);
    if (RHP_IS_ERROR(rv)) {
	Free(*addr);
	*addr = NULL;
	return False;
    }

    return True;
}