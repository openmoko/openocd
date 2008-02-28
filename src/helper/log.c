/***************************************************************************
 *   Copyright (C) 2005 by Dominic Rath                                    *
 *   Dominic.Rath@gmx.de                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "log.h"
#include "configuration.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

int debug_level = -1;

static FILE* log_output;
static log_callback_t *log_callbacks = NULL;

static time_t start;

static char *log_strings[5] =
{
	"User:   ",
	"Error:  ",
	"Warning:",
	"Info:   ",
	"Debug:  "
};

static int count = 0;

static void log_printfv(enum log_levels level, const char *file, int line, const char *function, const char *format, va_list args)
{
	char buffer[512];
	log_callback_t *cb;

	vsnprintf(buffer, 512, format, args);

	if (level == LOG_OUTPUT)
	{
		/* do not prepend any headers, just print out what we were given and return */
		fputs(buffer, log_output);
		fflush(log_output);
		return;
	}

	char *f = strrchr(file, '/');
	if (f != NULL)
		file = f + 1;

	if (debug_level >= LOG_DEBUG)
	{
		/* print with count and time information */
		int t=(int)(time(NULL)-start);
		fprintf(log_output, "%s %d %d %s:%d %s(): %s\n", log_strings[level+1], count, t, file, line, function, buffer);
	}
	else
	{
		/* do not print count and time */
		fprintf(log_output, "%s %s:%d %s(): %s\n", log_strings[level+1], file, line, function, buffer);
	}

	fflush(log_output);
	
	/* Never forward LOG_DEBUG, too verbose and they can be found in the log if need be */
	if (level <= LOG_INFO)
	{
		for (cb = log_callbacks; cb; cb = cb->next)
		{
			cb->fn(cb->priv, file, line, function, format, args);
		}
	}
}

void log_printf(enum log_levels level, const char *file, int line, const char *function, const char *format, ...)
{
	count++;
	if (level > debug_level)
		return;

	va_list args;
	va_start(args, format);
	log_printfv(level, file, line, function, format, args);
	va_end(args);
	
}

void log_printfnl(enum log_levels level, const char *file, int line, const char *function, const char *format, ...)
{
	count++;
	if (level > debug_level)
		return;
	
	char *t=malloc(strlen(format)+2);
	strcpy(t, format);
	strcat(t, "\n");
	
	va_list args;
	va_start(args, format);
	log_printfv(level, file, line, function, t, args);
	va_end(args);
}

/* change the current debug level on the fly
 * 0: only ERRORS
 * 1: + WARNINGS
 * 2: + INFORMATIONAL MSGS
 * 3: + DEBUG MSGS
 */
int handle_debug_level_command(struct command_context_s *cmd_ctx, char *cmd, char **args, int argc)
{
	if (argc == 0)
		command_print(cmd_ctx, "debug_level: %i", debug_level);

	if (argc > 0)
		debug_level = strtoul(args[0], NULL, 0);

	if (debug_level < 0)
		debug_level = 0;

	if (debug_level > 3)
		debug_level = 3;

	return ERROR_OK;
}

int handle_log_output_command(struct command_context_s *cmd_ctx, char *cmd, char **args, int argc)
{
	if (argc == 1)
	{
		FILE* file = fopen(args[0], "w");
		
		if (file)
		{
			log_output = file;
		}
	}

	return ERROR_OK;
}

int log_register_commands(struct command_context_s *cmd_ctx)
{
	start = time(NULL);
	register_command(cmd_ctx, NULL, "log_output", handle_log_output_command,
		COMMAND_ANY, "redirect logging to <file> (default: stderr)");
	register_command(cmd_ctx, NULL, "debug_level", handle_debug_level_command,
		COMMAND_ANY, "adjust debug level <0-3>");

	return ERROR_OK;
}

int log_init(struct command_context_s *cmd_ctx)
{
	/* set defaults for daemon configuration, if not set by cmdline or cfgfile */
	if (debug_level == -1)
		debug_level = LOG_INFO;
	
	if (log_output == NULL)
	{
		log_output = stderr;
	}
	
	return ERROR_OK;
}
	
int set_log_output(struct command_context_s *cmd_ctx, FILE *output)
{
	log_output = output;
	return ERROR_OK;
}

/* add/remove log callback handler */
int log_add_callback(log_callback_fn fn, void *priv)
{
	log_callback_t *cb;

	/* prevent the same callback to be registered more than once, just for sure */
	for (cb = log_callbacks; cb; cb = cb->next)
	{
		if (cb->fn == fn && cb->priv == priv)
			return ERROR_INVALID_ARGUMENTS;
	}

	/* alloc memory, it is safe just to return in case of an error, no need for the caller to check this */
	if ((cb = malloc(sizeof(log_callback_t))) == NULL)
		return ERROR_BUF_TOO_SMALL;

	/* add item to the beginning of the linked list */
	cb->fn = fn;
	cb->priv = priv;
	cb->next = log_callbacks;
	log_callbacks = cb;

	return ERROR_OK;
}

int log_remove_callback(log_callback_fn fn, void *priv)
{
	log_callback_t *cb, **p;

	for (p = &log_callbacks; (cb = *p); p = &(*p)->next)
	{
	    if (cb->fn == fn && cb->priv == priv)
	    {
	        *p = cb->next;
			free(cb);
			return ERROR_OK;
		}
	}

	/* no such item */
	return ERROR_INVALID_ARGUMENTS;
}

/* return allocated string w/printf() result */
char *alloc_printf(const char *fmt, va_list ap)
{
	char *string = NULL;
	
	/* start by 0 to exercise all the code paths. Need minimum 2 bytes to
	 * fit 1 char and 0 terminator. */
	int size = 0;
	int first = 1;
	for (;;)
	{
		if ((string == NULL) || (!first))
		{
			size = size * 2 + 2;
			char *t = string;
			string = realloc(string, size);
			if (string == NULL)
			{
				if (t != NULL)
					free(t);
				return NULL;
			}
		}
	
	    int ret;
	    ret = vsnprintf(string, size, fmt, ap);
	    /* NB! The result of the vsnprintf() might be an *EMPTY* string! */
	    if ((ret >= 0) && ((ret + 1) < size))
	    {
	    	return string;
	    }
	    /* there was just enough or not enough space, allocate more. */
	    first = 0;
	}
}
