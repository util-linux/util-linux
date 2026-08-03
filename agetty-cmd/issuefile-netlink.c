/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "agetty.h"
#include "c.h"

#ifdef USE_NETLINK
# include <net/if.h>
# include <arpa/inet.h>

static void print_iface_best(struct agetty_issue *ie,
			     const char *ifname,
			     uint8_t ifa_family)
{
	struct ul_netaddrq_ip *best[__ULNETLINK_RATING_MAX];
	struct ul_netaddrq_iface *ifaceq;
	struct list_head *l;
	enum ul_netaddrq_ip_rating threshold;

	if (!ie->nl.data_addr)
		return; /* error: init failed */

	if ((ifaceq = ul_netaddrq_iface_by_name(&(ie->nl), ifname)))
	{
		memset(best, 0, sizeof(best));
		if (ifa_family == AF_INET)
			l = &(ifaceq->ip_quality_list_4);
		else
		/* if (ifa_family == AF_INET6) */
			l = &(ifaceq->ip_quality_list_6);

		threshold =
			ul_netaddrq_iface_bestaddr(l, &best);
		if (threshold != __ULNETLINK_RATING_MAX)
			fputs(ul_nl_addr_ntop_address(best[threshold]->addr),
			      ie->output);
	}
}

static void print_addrq_bestofall(struct agetty_issue *ie,
				  uint8_t ifa_family)
{
	struct ul_netaddrq_iface *best_ifaceq;
	enum ul_netaddrq_ip_rating threshold;
	const char *best_ipp;

	if (!ie->nl.data_addr)
		return; /* error: init failed */

	best_ipp = ul_netaddrq_get_best_ipp(&(ie->nl), ifa_family,
					    &threshold, &best_ifaceq);
	if (best_ipp)
		fputs(best_ipp, ie->output);
}

static void dump_iface_good(struct agetty_issue *ie,
			    struct ul_netaddrq_iface *ifaceq)
{
	struct ul_netaddrq_ip *best4[__ULNETLINK_RATING_MAX];
	struct ul_netaddrq_ip *best6[__ULNETLINK_RATING_MAX];
	struct list_head *li;
	enum ul_netaddrq_ip_rating threshold = __ULNETLINK_RATING_MAX - 1;
	enum ul_netaddrq_ip_rating fthreshold; /* per family threshold */
	bool first = true;

	memset(best4, 0, sizeof(best4));
	threshold = ul_netaddrq_iface_bestaddr(&(ifaceq->ip_quality_list_4),
					       &best4);
	memset(best6, 0, sizeof(best6));
	fthreshold = ul_netaddrq_iface_bestaddr(&(ifaceq->ip_quality_list_6),
						&best6);
	if (fthreshold < threshold)
		threshold = fthreshold;

	list_for_each(li, &(ifaceq->ip_quality_list_4))
	{
		struct ul_netaddrq_ip *ipq;

		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (threshold <= ULNETLINK_RATING_SCOPE_LINK &&
		    ( ipq->quality <= threshold ||
		      /* Consider site addresses equally good as global */
		      ipq->quality == ULNETLINK_RATING_SCOPE_SITE) &&
		    best4[threshold])
		{
			if (first)
			{
				fprintf(ie->output, "%s: ", ifaceq->ifname);
				first = false;
			}
			else
				fprintf(ie->output, " ");
			/* Write only the longest living temporary address */
			if (threshold == ULNETLINK_RATING_F_TEMPORARY)
			{
				fputs(ul_nl_addr_ntop_address(best4[ULNETLINK_RATING_F_TEMPORARY]->addr),
				      ie->output);
				goto temp_cont4;
			}
			else
				fputs(ul_nl_addr_ntop_address(ipq->addr),
				      ie->output);
		}
	temp_cont4:;
	}

	list_for_each(li, &(ifaceq->ip_quality_list_6))
	{
		struct ul_netaddrq_ip *ipq;

		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (threshold <= ULNETLINK_RATING_SCOPE_LINK &&
		    ( ipq->quality <= threshold ||
		      /* Consider site addresses equally good as global */
		      ipq->quality == ULNETLINK_RATING_SCOPE_SITE) &&
		    best6[threshold])
		{
			if (first)
			{
				fprintf(ie->output, "%s: ", ifaceq->ifname);
				first = false;
			}
			else
				fprintf(ie->output, " ");
			/* Write only the longest living temporary address */
			if (threshold == ULNETLINK_RATING_F_TEMPORARY)
			{
				fputs(ul_nl_addr_ntop_address(best6[ULNETLINK_RATING_F_TEMPORARY]->addr),
				      ie->output);
				goto temp_cont6;
			}
			else
				fputs(ul_nl_addr_ntop_address(ipq->addr),
				      ie->output);
		}
	temp_cont6:;
	}
	if (!first)
		fputs("\n", ie->output);
}

static void dump_iface_all(struct agetty_issue *ie,
			   struct ul_netaddrq_iface *ifaceq)
{
	struct list_head *li;
	struct ul_netaddrq_ip *ipq;
	bool first = true;

	list_for_each(li, &(ifaceq->ip_quality_list_4))
	{
		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (first)
		{
			fprintf(ie->output, "%s: ", ifaceq->ifname);
			first = false;
		}
		else
			fprintf(ie->output, " ");
		fputs(ul_nl_addr_ntop_address(ipq->addr), ie->output);
	}
	list_for_each(li, &(ifaceq->ip_quality_list_6))
	{
		ipq = list_entry(li, struct ul_netaddrq_ip, entry);
		if (first)
		{
			fprintf(ie->output, "%s: ", ifaceq->ifname);
			first = false;
		}
		else
			fprintf(ie->output, " ");
		fputs(ul_nl_addr_ntop_address(ipq->addr), ie->output);
	}
	if (!first)
		fputs("\n", ie->output);
}

static int print_ipaddr(struct agetty_iitem *item,
			struct agetty_issue *ie,
			struct agetty_ihandler *handler __attribute__((__unused__)))
{
	const char *iface = agetty_iitem_get_arg(item, "interface");
	uint8_t family;

	if (!iface)
		iface = agetty_iitem_get_arg(item, NULL);

	family = agetty_iitem_get_id(item) == AGETTY_ESC_IPV4 ?
			AF_INET : AF_INET6;

	if (iface)
		print_iface_best(ie, iface, family);
	else
		print_addrq_bestofall(ie, family);
	return 0;
}

static int print_net_good(struct agetty_iitem *item __attribute__((__unused__)),
			  struct agetty_issue *ie,
			  struct agetty_ihandler *handler __attribute__((__unused__)))
{
	struct list_head *li;
	struct ul_netaddrq_iface *ifaceq;

	list_for_each_netaddrq_iface(li, &(ie->nl))
	{
		ifaceq = list_entry(li, struct ul_netaddrq_iface, entry);
		dump_iface_good(ie, ifaceq);
	}
	return 0;
}

static int print_net_all(struct agetty_iitem *item __attribute__((__unused__)),
			 struct agetty_issue *ie,
			 struct agetty_ihandler *handler __attribute__((__unused__)))
{
	struct list_head *li;
	struct ul_netaddrq_iface *ifaceq;

	list_for_each_netaddrq_iface(li, &(ie->nl))
	{
		ifaceq = list_entry(li, struct ul_netaddrq_iface, entry);
		dump_iface_all(ie, ifaceq);
	}
	return 0;
}

static void init_netlink(struct agetty_issue *ie)
{
	uint32_t netlink_groups = RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

	if (ie->nl.fd >= 0)
		return;

	ul_nl_init(&(ie->nl));
	if ((ul_netaddrq_init(&(ie->nl), NULL, NULL, (void *)ie)))
		return;

	if (ul_nl_open(&(ie->nl), RTMGRP_LINK | netlink_groups))
		return;
	if (ul_nl_request_dump(&(ie->nl), RTM_GETADDR))
		goto error;
	if (ul_nl_process(&(ie->nl), UL_NL_SYNC, UL_NL_LOOP) != UL_NL_DONE)
		goto error;
	return;
error:
	ul_nl_close(&(ie->nl));
	ie->nl.fd = -1;
}

void agetty_issue_register_netlink(struct agetty_issue *ie)
{
	struct agetty_ifile *ls = &ie->ifile;

	if (!(agetty_ifile_get_mask(ls) & (BIT(AGETTY_ESC_IPV4) |
					   BIT(AGETTY_ESC_IPV6) |
					   BIT(AGETTY_ESC_NET_GOOD) |
					   BIT(AGETTY_ESC_NET_ALL))))
		return;

	init_netlink(ie);

	agetty_ifile_set_handler(ls, AGETTY_ESC_IPV4, print_ipaddr, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_IPV6, print_ipaddr, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_NET_GOOD, print_net_good, NULL, NULL);
	agetty_ifile_set_handler(ls, AGETTY_ESC_NET_ALL, print_net_all, NULL, NULL);
}

#else /* !USE_NETLINK */

void agetty_issue_register_netlink(struct agetty_issue *ie __attribute__((__unused__)))
{
}

#endif /* USE_NETLINK */
