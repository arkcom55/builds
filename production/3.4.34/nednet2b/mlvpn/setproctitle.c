/* Based on conf.c from UCB sendmail 8.8.8 */

/*
 * Copyright 2003 Damien Miller
 * Copyright (c) 1983, 1995-1997 Eric P. Allman
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "includes.h"
#ifndef HAVE_SETPROCTITLE

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "vis.h"

#define SPT_PADCHAR	'\0'

static char *argv_start = NULL;
static size_t argv_env_len = 0;


#endif /* HAVE_SETPROCTITLE */

void
compat_init_setproctitle(int argc, char *argv[])
{
#ifndef HAVE_SETPROCTITLE
	extern char **environ;
	char *lastargv = NULL;
	char **envp = environ;
	int i;

	/*
	 * NB: This assumes that argv has already been copied out of the
	 * way. This is true for sshd, but may not be true for other 
	 * programs. Beware.
	 */

	if (argc == 0 || argv[0] == NULL)
		return;

	/* Fail if we can't allocate room for the new environment */
	for (i = 0; envp[i] != NULL; i++)
		;
	if ((environ = calloc(i + 1, sizeof(*environ))) == NULL) {
		environ = envp;	/* put it back */
		return;
	}

	/*
	 * Find the last argv string or environment variable within 
	 * our process memory area.
	 */
	for (i = 0; i < argc; i++) {
		if (lastargv == NULL || lastargv + 1 == argv[i])
			lastargv = argv[i] + strlen(argv[i]);
	}
	for (i = 0; envp[i] != NULL; i++) {
		if (lastargv + 1 == envp[i])
			lastargv = envp[i] + strlen(envp[i]);
	}

	argv[1] = NULL;
	argv_start = argv[0];
	argv_env_len = lastargv - argv[0] - 1;

	/* 
	 * Copy environment 
	 * XXX - will truncate env on strdup fail
	 */
	for (i = 0; envp[i] != NULL; i++)
		environ[i] = strdup(envp[i]);
	environ[i] = NULL;
#endif /* HAVE_SETPROCTITLE */
}

#ifndef HAVE_SETPROCTITLE
void
setproctitle(const char *fmt, ...)
{
	va_list ap;
	char buf[1024], ptitle[1024];
	size_t len;
	int r;
	extern char *_progname;

    //log_info("proc", "setproctitle()");
	if (argv_env_len <= 0) {
        if (argv_start) {
            strcpy(argv_start, "mlvpn_default");
            log_info("proc", "setproctitle:%s", argv_start);
        }
        return;
    } 

	mystrlcpy(buf, _progname, sizeof(buf));

	r = -1;
	va_start(ap, fmt);
	if (fmt != NULL) {
		len = mystrlcat(buf, ": ", sizeof(buf));
		if (len < sizeof(buf)) {
			r = vsnprintf(buf + len, sizeof(buf) - len , fmt, ap);
        }
	}
	va_end(ap);
	if (r == -1 || (size_t)r >= sizeof(buf) - len) {
        strcpy(argv_start, "mlvpn_default2");
        log_info("proc", "setproctitle:%s", argv_start);
        return;
    }
	strnvis(ptitle, buf, sizeof(ptitle),
	    VIS_CSTYLE|VIS_NL|VIS_TAB|VIS_OCTAL);

	len = mystrlcpy(argv_start, ptitle, argv_env_len);
	for(; len < argv_env_len; len++) {
		argv_start[len] = SPT_PADCHAR;
    }
    //log_info("proc", "setproctitle:%s", argv_start);
}
#endif /* HAVE_SETPROCTITLE */
