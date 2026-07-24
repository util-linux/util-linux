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

extern int isinteger(const char *str);
#if defined(__s390__) || defined(__s390x__)
extern int openvmcp(void);
extern void clearvmcp(void);
extern char* queryterm(int fd);
extern char* queryspool(int fd);
extern int setterm(int fd, char *tout);
extern int stopspool(int fd);
extern int restoreterm(int fd);
extern int restorespool(int fd);
extern void parseterm(char *msg);
extern void parsespool(char *msg);
extern void warning3215(int fd);
#endif
