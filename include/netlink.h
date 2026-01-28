/*
 * Netlink message processing
 *
 * Copyright (C) 2025 Stanislav Brabec <sbrabec@suse.com>
 *
 * This program is freely distributable.
 *
 * This set of functions processes netlink messages from the kernel socket,
 * joins message parts into a single structure and calls callback.
 *
 * To do something useful, callback for a selected message type has to be
 * defined. Using callback functions and custom data, it could be used for
 * arbitrary purpose.
 *
 * The code is incomplete. More could be implemented as needed by its use
 * cases.
 *
 */

#ifndef UTIL_LINUX_NETLINK_H
#define UTIL_LINUX_NETLINK_H

#include <stddef.h>
#include <stdbool.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include "list.h"

/* Return codes */
/* 0 means OK.
 * Negative return codes indicate fatal errors.
 */

#define	UL_NL_WOULDBLOCK 1  /* no data are ready (for asynchronous mode) */
#define	UL_NL_DONE	 2  /* processing reached NLMSG_DONE (for
			     * ul_nl_request_dump() */
#define	UL_NL_RETURN	 3  /* callback initiated immediate return; if you use
			     * it, keep in mind that further processing could
			     * reach unprocessed NLMSG_DONE */
#define	UL_NL_SOFT_ERROR 4  /* soft error, indicating a race condition or
			     * message relating to events before program
			     * start); could be optionally ignored and it
			     * should not considered as a reason to leave the
			     * processing */

struct ul_nl_data;

/* The callback of the netlink message header.
 * Return code: Normally returns UL_NL_OK. In other cases,
 *   ul_nl_process() immediately exits with an error.
 *   Special return codes:
 *   UL_NL_RETURN: stopping further processing that does not mean an error
 *     (example: There was found interface or IP we were waiting for.)
 * See <linux/netlink.h> nlmsghdr to see, what you can process here.
 */
typedef int (*ul_nl_callback)(struct ul_nl_data *nl);

/* Structure for ADDR messages collects information from a single ifaddsmsg
 * structure and all optional rtattr structures into a single structure
 * containing all useful data. */
struct ul_nl_addr {
/* values from ifaddrmsg or rtattr */
	uint8_t ifa_family;
	uint8_t ifa_scope;
	uint8_t ifa_index;
	uint32_t ifa_flags;
	void *ifa_address;	/* IFA_ADDRESS */
	int ifa_address_len;	/* size of IFA_ADDRESS data */
	void *ifa_local;	/* IFA_LOCAL */
	int ifa_local_len;	/* size of IFA_LOCAL data */
	char *ifname;		/* interface from ifa_index as string */
	void *address;		/* IFA_LOCAL, if defined, otherwise
				 * IFA_ADDRESS. This is what you want it most
				 * cases. See comment in linux/if_addr.h. */
	int address_len;	/* size of address data */
	uint32_t ifa_prefered;	/* ifa_prefered from IFA_CACHEINFO */
	uint32_t ifa_valid;	/* ifa_valid from IFA_CACHEINFO */
	/* More can be implemented in future. */
};

/* Values for rtm_event */
#define UL_NL_RTM_DEL false	/* processing RTM_DEL_* */
#define UL_NL_RTM_NEW true	/* processing RTM_NEW_* */
/* Checks for rtm_event */
#define UL_NL_IS_RTM_DEL(nl) (!(nl->rtm_event))	/* is it RTM_DEL_*? */
#define UL_NL_IS_RTM_NEW(nl) (nl->rtm_event)	/* is it RTM_NEW_*? */

struct ul_nl_data {
	/* "static" part of the structure, filled once and kept */
	ul_nl_callback callback_addr; /* Function to process ul_nl_addr */
	void *data_addr;	/* Arbitrary data of callback_addr */
	int fd;			/* netlink socket FD, may be used externally
				 * for select() */

	/* volatile part of the structure, filled by the current message */
	bool rtm_event;		/* UL_NL_RTM_DEL or UL_NL_RTM_NEW */
	bool dumping;		/* Dump in progress */

	/* volatile part of the structure that depends on message typ */
	union {
		/* ADDR */
		struct ul_nl_addr addr;
		/* More can be implemented in future (LINK, ROUTE etc.). */
	};
};

/* Initialize ul_nl_data structure */
void ul_nl_init(struct ul_nl_data *nl);

/* Open a netlink connection.
 * nl_groups: Applies for monitoring. In case of ul_nl_request_dump(),
 *   use its argument to select one.
 *
 * Close and open vs. initial open with parameters?
 *
 * If we use single open with parameters, we can get mixed output due to race
 * window between opening the socket and sending dump request.
 *
 * If we use close/open, we get a race window that could contain unprocessed
 * events.
 */
int ul_nl_open(struct ul_nl_data *nl, uint32_t nl_groups);

/* Close a netlink connection. */
int ul_nl_close(struct ul_nl_data *nl);

/* Synchronously sends dump request of a selected nlmsg_type. It does not
 * perform any further actions. The result is returned through the callback.
 *
 * Under normal conditions, use ul_nl_process(nl, false, true); for processing
 * the reply
 */
int ul_nl_request_dump(struct ul_nl_data *nl, uint16_t nlmsg_type);

/* Values for async */
#define UL_NL_SYNC false	/* synchronous mode */
#define UL_NL_ASYNC true	/* asynchronous mode */
#define UL_NL_ONESHOT false	/* return after processing message */
#define UL_NL_LOOP  true	/* wait for NLMSG_DONE */
/* Process netlink messages.
 * async: If true, return UL_NL_WOULDBLOCK immediately if there is no data
 *   ready. If false, wait for a message.
 *   NOTE: You should read all data until you get UL_NL_WOULDBLOCK, otherwise
 *         select() will not trigger even if there is a netlink message.
 * loop: If true, run in a loop until NLMSG_DONE is received. Returns after
 *   finishing a reply from ul_nl_request_dump(), otherwise it acts as an
 *   infinite loop. If false, it returns after processing one message.
 */
int ul_nl_process(struct ul_nl_data *nl, bool async, bool loop);

/* Duplicate ul_nl_addr structure to a newly allocated memory */
struct ul_nl_addr *ul_nl_addr_dup (struct ul_nl_addr *addr);

/* Deallocate ul_nl_addr structure */
void ul_nl_addr_free (struct ul_nl_addr *addr);

/* Convert ul_nl_addr to string.
   addr: ul_nl_addr structure
   id: Which of 3 possible addresses should be converted?
 * Returns static string, valid to next call.
 */
#define UL_NL_ADDR_ADDRESS offsetof(struct ul_nl_addr, address)
#define UL_NL_ADDR_IFA_ADDRESS offsetof(struct ul_nl_addr, ifa_address)
#define UL_NL_ADDR_IFA_LOCAL offsetof(struct ul_nl_addr, ifa_local)
/* Warning: id must be one of above. No checks are performed */
const char *ul_nl_addr_ntop (const struct ul_nl_addr *addr, int addrid);
#define ul_nl_addr_ntop_address(addr)\
  ul_nl_addr_ntop(addr, UL_NL_ADDR_ADDRESS)
#define ul_nl_addr_ntop_ifa_address(addr)\
  ul_nl_addr_ntop(addr, UL_NL_ADDR_IFA_ADDRESS)
 #define ul_nl_addr_ntop_ifa_local(addr)\
   ul_nl_addr_ntop(addr, UL_NL_ADDR_IFA_LOCAL)

#endif /* UTIL_LINUX_NETLINK_H */
