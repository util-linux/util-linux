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
#include "vmcp.h"

#if defined(__s390__) || defined(__s390x__)

#define	VMCP_DEVICE_NODE	"/dev/vmcp"
#define	VMCP_GETSIZE		_IOR(0x10, 3, int)
#define	VMCP_SETBUF		_IOW(0x10, 2, int)
#define	VMCP_GETCODE		_IOR(0x10, 1, int)

static char *more, *hold;
static int spooling;

int openvmcp(void)
{
	return open(VMCP_DEVICE_NODE, O_RDWR | O_NOCTTY);
}

void clearvmcp(void)
{
	if (more) {
		free(more);
		more = NULL;
	}
	if (hold) {
		free(hold);
		hold = NULL;
	}
}

static char *askvmcp(int fd, const char *question)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	int rc = 0, num, buffersize;
	char *ret = NULL;

	num = (strlen(question) + pagesize - 1) / pagesize;
	buffersize = num * pagesize;

	if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
		goto out;
	if (ul_write_all(fd, question, strlen(question)) != 0)
		goto out;
	if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
		goto out;
	if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1)
		goto out;
	ret = (char *)malloc(buffersize);
	if (!ret)
		goto out;
	if (ul_read_all(fd, ret, buffersize) != 0) {
		free(ret);
		ret = NULL;
	}
 out:
	return ret;
}

char *queryterm(int fd)
{
	const char *question = "QUERY TERMINAL";
	/* Reset cached terminal state before reparsing a fresh QUERY TERMINAL reply. */
	clearvmcp();
	return askvmcp(fd, question);
}

char *queryspool(int fd)
{
	const char *question = "QUERY VIRTUAL CONSOLE";
	return askvmcp(fd, question);
}

static int writevmcp(int fd, char *instruction)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	int rc = 0, num, buffersize;
	int ret = -1;

	num = (strlen(instruction) + pagesize - 1) / pagesize;
	buffersize = num * pagesize;

	if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
		goto out;
	do {
		rc = write(fd, instruction, strlen(instruction));
		if (rc < 0) {
			if (errno != EINTR)
				goto out;
		}
	} while (rc < 0);
	if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
		goto out;
	if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1)
		goto out;
	if (rc == 0 && buffersize == 0)
		ret = 0;
 out:
	return ret;
}

int setterm(int fd, char *tout)
{
	char *instruction;
	int ret = -1;

	if (asprintf(&instruction, "TERMINAL MORE %s 0 HOLD OFF", tout) == -1)
		goto out;

	ret = writevmcp(fd, instruction);
	free(instruction);
 out:
	return ret;
}

int restoreterm(int fd)
{
	char *instruction;
	int ret = -1;

	if (!more || !hold)
		goto out;
	if (asprintf(&instruction, "TERMINAL %s %s", more, hold) == -1)
		goto out;

	ret = writevmcp(fd, instruction);
	free(instruction);
 out:
	return ret;
}

int stopspool(int fd)
{
	char *instruction = "SPOOL CONSOLE STOP";
	if (!spooling)
		return 1;
	return writevmcp(fd, instruction);
}

int restorespool(int fd)
{
	char *instruction = "SPOOL CONSOLE START";
	if (!spooling)
		return 1;
	return writevmcp(fd, instruction);
}

void parseterm(char *msg)
{
	char *token;

	for (token = strtok(msg, ",\n"); token != NULL;
	     token = strtok(NULL, ",\n")) {
		if (hold && more)
			break;

		while (*token == ' ')
			token++;

		if (strncmp("MORE ", token, 5) == 0)
			more = strdup(token);
		if (strncmp("HOLD ", token, 5) == 0)
			hold = strdup(token);
	}
}

void parsespool(char *msg)
{
	spooling = 0;
	if (strstr(msg, " TERM START ") != NULL)
		spooling = 1;
}

void warning3215(int fd)
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
#endif
