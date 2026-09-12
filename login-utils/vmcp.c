/*
 * SPDX-License-Identifier: MIT
 *
 * vmcp.c - s390x 3215 console spool and terminal management
 *
 * Copyright 2026 Werner Fink, SUSE Software Solutions Germany GmbH
 *
 * Based on:
 *
 * Copyright IBM Corp. 2018
 * s390-tools is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "all-io.h"
#include "xalloc.h"
#include "vmcp.h"
#include "sulogin-consoles.h"

#if defined(__s390__) || defined(__s390x__)

#define	VMCP_DEVICE_NODE	"/dev/vmcp"
#define	VMCP_GETSIZE		_IOR(0x10, 3, int)
#define	VMCP_SETBUF		_IOW(0x10, 2, int)
#define	VMCP_GETCODE		_IOR(0x10, 1, int)

static struct {
	char *more;
	char *hold;
	int spooling;
} vmcp_state;

int vmcp_open(void)
{
	return open(VMCP_DEVICE_NODE, O_RDWR | O_NOCTTY | O_CLOEXEC);
}

static void clearvmcp(void)
{
	if (vmcp_state.more) {
		free(vmcp_state.more);
		vmcp_state.more = NULL;
	}
	if (vmcp_state.hold) {
		free(vmcp_state.hold);
		vmcp_state.hold = NULL;
	}
}

static char *askvmcp(int fd, const char *question)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	int rc = 0;
	size_t buffersize;
	char *ret = NULL;

	if (pagesize <= 0)
		return NULL;

	buffersize = (size_t)pagesize;

	if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
		goto out;
	if (ul_write_all(fd, question, strlen(question)) != 0)
		goto out;
	if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
		goto out;
	if (rc != 0)
		goto out;
	if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1 || buffersize == 0)
		goto out;
	ret = (char *)xmalloc(buffersize+1);
	ret[buffersize] = '\0';
	if (ul_read_all(fd, ret, buffersize) != 0) {
		free(ret);
		ret = NULL;
	}
 out:
	return ret;
}

static char *queryterm(int fd)
{
	const char *question = "QUERY TERMINAL";
	/* Reset cached terminal state before reparsing a fresh QUERY TERMINAL reply. */
	clearvmcp();
	return askvmcp(fd, question);
}

static char *queryspool(int fd)
{
	const char *question = "QUERY VIRTUAL CONSOLE";
	return askvmcp(fd, question);
}

static int writevmcp(int fd, const char *instruction)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	int rc = 0;
	size_t buffersize;
	int ret = -1;

	if (pagesize <= 0)
		goto out;

	buffersize = (size_t)pagesize;

	if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
		goto out;
	if (ul_write_all(fd, instruction, strlen(instruction)) != 0)
		goto out;
	if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
		goto out;
	if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1)
		goto out;
	if (rc == 0 && buffersize == 0)
		ret = 0;
 out:
	return ret;
}

static int setterm(int fd, const char *tout)
{
	char *instruction;
	int ret;

	/*
	 * Note on z/VM CP command syntax:
	 * TERMINAL MORE <initial_time> <interval_time> HOLD <ON|OFF>
	 * The '0' is the mandatory interval_time parameter. Setting both
	 * times to 0 (and HOLD OFF) prevents the console from entering
	 * the "MORE..." state and waiting for user input (e.g. pressing CLEAR)
	 * during the password prompt.
	 */
	xasprintf(&instruction, "TERMINAL MORE %s 0 HOLD OFF", tout);

	ret = writevmcp(fd, instruction);
	free(instruction);

	return ret;
}

static int restoreterm(int fd)
{
	char *instruction;
	int ret = -1;

	if (!vmcp_state.more || !vmcp_state.hold)
		goto out;
	xasprintf(&instruction, "TERMINAL %s %s", vmcp_state.more, vmcp_state.hold);

	ret = writevmcp(fd, instruction);
	free(instruction);
 out:
	return ret;
}

static int stopspool(int fd)
{
	const char *instruction = "SPOOL CONSOLE STOP";
	if (!vmcp_state.spooling)
		return 1;
	return writevmcp(fd, instruction);
}

static int restorespool(int fd)
{
	const char *instruction = "SPOOL CONSOLE START";
	if (!vmcp_state.spooling)
		return 1;
	return writevmcp(fd, instruction);
}

static void parseterm(char *msg)
{
	char *token;

	for (token = strtok(msg, ",\n"); token != NULL;
	     token = strtok(NULL, ",\n")) {
		if (vmcp_state.hold && vmcp_state.more)
			break;

		while (*token == ' ')
			token++;

		if (strncmp("MORE ", token, 5) == 0) {
			free(vmcp_state.more);
			vmcp_state.more = xstrdup(token);
		}
		if (strncmp("HOLD ", token, 5) == 0) {
			free(vmcp_state.hold);
			vmcp_state.hold = xstrdup(token);
		}
	}
}

static void parsespool(char *msg)
{
	vmcp_state.spooling = 0;
	if (strstr(msg, " TERM START ") != NULL)
		vmcp_state.spooling = 1;
}

void vmcp_warning3215(int fd)
{
	/*
	 * Warning: Do not translate this test as it might inlude then so called
	 * umlauts which in fact can not be encoded for the 3215 console interface.
	 * The 3215 console driver work in fact with EBCDIC codepage and the
	 * kernel has to translate such umlauts (multi or single bytes) with the
	 * correct EBCDIC character table (for german e.g. IBM-1141 or IBM-273).
	 */
	(void)writevmcp(fd, "MESSAGE * WARNING: 3215 mode. Password visible!");
	(void)writevmcp(fd, "MESSAGE * Ensure nobody is watching the screen.");
}

/* Query spool state, parse it, and stop logging */
void vmcp_stop_console_logging(int fd)
{
	char *msg = queryspool(fd);
	if (msg) {
		parsespool(msg);
		free(msg);
	}
	if (vmcp_state.spooling)
		stopspool(fd);
}

/* Query terminal state, parse it, and configure for password input */
void vmcp_prepare_terminal_for_password(int fd)
{
	char *msg = queryterm(fd);
	if (msg) {
		parseterm(msg);
		free(msg);
	}
	if (vmcp_state.more && vmcp_state.hold)
		setterm(fd, "0");
}

/* Restore terminal and spool states, cleanup, and close fd */
void vmcp_restore_and_close(int fd, int flags)
{
	if (fd < 0)
		return;
	if (flags & CON_3215)
		restoreterm(fd);
	if (flags & (CON_3215|CON_3270))
		restorespool(fd);
	clearvmcp();
	close(fd);
}
#endif
