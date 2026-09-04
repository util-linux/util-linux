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

#if defined(__s390__) || defined(__s390x__)
extern int openvmcp(void);
extern void warning3215(int fd);
extern void vmcp_stop_console_logging(int fd);
extern void vmcp_prepare_terminal_for_password(int fd);
extern void vmcp_restore_and_close(int fd, int flags);
#endif
