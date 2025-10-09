/*
 * Netlink address quality rating list builder
 *
 * Copyright (C) 2025 Stanislav Brabec <sbrabec@suse.com>
 *
 * This program is freely distributable.
 *
 * This set of netlink callbacks kernel and creates
 * and/or maintains a linked list of requested type. Using callback fuctions
 * and custom data, it could be used for arbitraty purpose.
 *
 */

#ifndef UTIL_LINUX_NETADDRQ_H
#define UTIL_LINUX_NETADDRQ_H

#include "netlink.h"

/* Specific return code */
#define	UL_NL_IFACES_MAX	 64	/* ADDR: Too many interfaces */

/* Network address "quality". Higher means worse. */
enum ul_netaddrq_ip_rating {
	ULNETLINK_RATING_SCOPE_UNIVERSE,
	ULNETLINK_RATING_SCOPE_SITE,
	ULNETLINK_RATING_F_TEMPORARY,
	ULNETLINK_RATING_SCOPE_LINK,
	ULNETLINK_RATING_BAD,
	__ULNETLINK_RATING_MAX
};

/* Data structure in ul_nl_data You can use callback_pre for filtering events
 * you want to get into the list, callback_post to check the processed data or
 * use the list after processing
 */
struct ul_netaddrq_data {
	ul_nl_callback callback_pre;  /* Function to process ul_netaddrq_data */
	ul_nl_callback callback_post; /* Function to process ul_netaddrq_data */
	void *callback_data;	      /* Arbitrary data for callback */
	struct list_head ifaces;      /* The intefaces list */
	/* ifaces_change_* has to be changed by userspace when processed. */
	bool ifaces_change_4;	      /* Any changes in the IPv4 list? */
	bool ifaces_change_6;	      /* Any changes in the IPv6 list? */
	int nifaces;		      /* interface count */
	bool overflow;		      /* Too many interfaces? */
};

/* List item for particular interface contains interface specific data and
 * heads of two lists, one per each address family */
struct ul_netaddrq_iface {
	struct list_head entry;
	uint32_t ifa_index;
	char *ifname;
	struct list_head ip_quality_list_4;
	struct list_head ip_quality_list_6;
};

/* Macro casting generic ul_nl_data->data_addr to struct ul_netaddrq_data */
#define UL_NETADDRQ_DATA(nl) ((struct ul_netaddrq_data*)((nl)->data_addr))

/* list_for_each macro for intercaces */
#define list_for_each_netaddrq_iface(li, nl) list_for_each(li, &(UL_NETADDRQ_DATA(nl)->ifaces))

/* List item for for a particular address contains information for IP quality
 * evaluation and a copy of generic ul_nl_addr data */
struct ul_netaddrq_ip {
	struct list_head entry;
	enum ul_netaddrq_ip_rating quality;
	struct ul_nl_addr *addr;
};

/* Initialize ul_nl_data for use with netlink-addr-quality
 * callback: Process the data after updating the tree. If NULL, it just
 *   updates the tree and everything has to be processed outside.
 */
int ul_netaddrq_init(struct ul_nl_data *nl, ul_nl_callback callback_pre,
		     ul_nl_callback callback_post, void *data);

/* Get best rating value from the ul_netaddrq_ip list
 * ipq_list: List of IP addresses of a particular interface and family
 * returns:
 *   best array: best ifa_valid lifetime seen per quality rating
 *   return value: best rating seen
 * Note: It can be needed to call it twice: once for ip_quality_list_4, once
 * for ip_quality_list_6.
 */
enum ul_netaddrq_ip_rating
ul_netaddrq_iface_bestaddr(struct list_head *ipq_list,
			   struct ul_netaddrq_ip *(*best)[__ULNETLINK_RATING_MAX]);

/* Get best rating value from the ifaces list (i. e. best address of all
 * interfaces)
 * returns:
 *   best_iface: interface where the best address was seen
 *   best array: best ifa_valid lifetime seen per quality rating
 *   return value: best rating seen
 * Notes:
 * - It can be needed to call it twice: once for ip_quality_list_4, once
 *   for ip_quality_list_6.
 * - If no IP addresses are found, the function can return
 *   _ULNETLINK_RATING_MAX!
 */
enum ul_netaddrq_ip_rating
ul_netaddrq_bestaddr(struct ul_nl_data *nl,
		     struct ul_netaddrq_iface **best_iface,
		     struct ul_netaddrq_ip *(*best)[__ULNETLINK_RATING_MAX],
		     uint8_t ifa_family);

/* Get best rating value from the ul_netaddrq_ip list as a string
 * ipq_list: List of IP addresses of a particular interface and family
 * returns:
 *   return value: The best address as a string
 *   threshold: The best rating ever seen.
 *   best_ifaceq: The best rated interfece ever seen.
 * Notes:
 * - It can be needed to call it twice: once for AF_INET, once
 *   for AF_INET6.
 * - If the return value is NULL (i. e. there are no usable interfaces), then
 *   *best_ifaceq remains unchanges and cannot be used.
 */
const char *ul_netaddrq_get_best_ipp(struct ul_nl_data *nl,
				     uint8_t ifa_family,
				     enum ul_netaddrq_ip_rating *threshold,
				     struct ul_netaddrq_iface **best_ifaceq);

/* Find interface by name */
struct ul_netaddrq_iface *ul_netaddrq_iface_by_name(const struct ul_nl_data *nl,
						    const char *ifname);

#endif /* UTIL_LINUX_NETADDRQ_H */
