
/*
 * Interface functions.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 * (C)2024-2025 Hikaru Yamatohimemiya
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your
 * option) any later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <zebra.h>

#include "linklist.h"
#include "vector.h"
#include "vty.h"
#include "command.h"
#include "vrf.h"
#include "if.h"
#include "sockunion.h"
#include "prefix.h"
#include "memory.h"
#include "table.h"
#include "buffer.h"
#include "str.h"
#include "log.h"

/* List of interfaces in only the default VRF */
struct list *iflist;

/* One for each program.  This structure is needed to store hooks. */
struct if_master {
	int (*if_new_hook)(struct interface *);
	int (*if_delete_hook)(struct interface *);
} if_master = {
	0,
};

/* Compare interface names, returning an integer greater than, equal to, or
 * less than 0, (following the strcmp convention), according to the
 * relationship between ifp1 and ifp2.  Interface names consist of an
 * alphabetic prefix and a numeric suffix.  The primary sort key is
 * lexicographic by name, and then numeric by number.  No number sorts
 * before all numbers.  Examples: de0 < de1, de100 < fxp0 < xl0, devpty <
 * devpty0, de0 < del0
 */
int if_cmp_func(struct interface *ifp1, struct interface *ifp2) {
	unsigned int l1, l2;
	long int x1, x2;
	char *p1, *p2;
	int res;

	p1 = ifp1->name;
	p2 = ifp2->name;

	while(*p1 && *p2) {
		/* look up to any number */
		l1 = strcspn(p1, "0123456789");
		l2 = strcspn(p2, "0123456789");

		/* name lengths are different -> compare names */
		if(l1 != l2) {
			return (strcmp(p1, p2));
		}

		/* Note that this relies on all numbers being less than all letters, so
     * that de0 < del0.
     */
		res = strncmp(p1, p2, l1);

		/* names are different -> compare them */
		if(res) {
			return res;
		}

		/* with identical name part, go to numeric part */
		p1 += l1;
		p2 += l1;

		if(!*p1) {
			return -1;
		}
		if(!*p2) {
			return 1;
		}

		x1 = strtol(p1, &p1, 10);
		x2 = strtol(p2, &p2, 10);

		/* let's compare numbers now */
		if(x1 < x2) {
			return -1;
		}
		if(x1 > x2) {
			return 1;
		}

		/* numbers were equal, lets do it again..
    (it happens with name like "eth123.456:789") */
	}
	if(*p1) {
		return 1;
	}
	if(*p2) {
		return -1;
	}
	return 0;
}

/* Create new interface structure. */
struct interface *if_create_vrf(const char *name, int namelen, vrf_id_t vrf_id) {
	struct interface *ifp;
	struct list *intf_list = vrf_iflist_get(vrf_id);

	ifp = XCALLOC(MTYPE_IF, sizeof(struct interface));
	ifp->ifindex = IFINDEX_INTERNAL;

	assert(name);
	assert(namelen <= INTERFACE_NAMSIZ); /* Need space for '\0' at end. */
	strncpy(ifp->name, name, namelen);
	ifp->name[namelen] = '\0';
	ifp->vrf_id = vrf_id;
	if(if_lookup_by_name_vrf(ifp->name, vrf_id) == NULL) {
		listnode_add_sort(intf_list, ifp);
	} else {
		zlog_err(
			"if_create(%s): corruption detected -- interface with this "
			"name exists already in VRF %u!",
			ifp->name, vrf_id
		);
	}
	ifp->connected = list_new();
	ifp->connected->del = (void (*)(void *)) connected_free;

	if(if_master.if_new_hook) {
		(*if_master.if_new_hook)(ifp);
	}

	return ifp;
}

struct interface *if_create(const char *name, int namelen) {
	return if_create_vrf(name, namelen, VRF_DEFAULT);
}

/* Delete interface structure. */
void if_delete_retain(struct interface *ifp) {
	if(if_master.if_delete_hook) {
		(*if_master.if_delete_hook)(ifp);
	}

	/* Free connected address list */
	list_delete_all_node(ifp->connected);
}

/* Delete and free interface structure. */
void if_delete(struct interface *ifp) {
	listnode_delete(vrf_iflist(ifp->vrf_id), ifp);

	if_delete_retain(ifp);

	list_free(ifp->connected);

	if_link_params_free(ifp);

	XFREE(MTYPE_IF, ifp);
}

/* Add hook to interface master. */
void if_add_hook(int type, int (*func)(struct interface *ifp)) {
	switch(type) {
		case IF_NEW_HOOK: if_master.if_new_hook = func; break;
		case IF_DELETE_HOOK: if_master.if_delete_hook = func; break;
		default: break;
	}
}

/* Interface existance check by index. */
struct interface *if_lookup_by_index_vrf(ifindex_t ifindex, vrf_id_t vrf_id) {
	struct listnode *node;
	struct interface *ifp;

	for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
		if(ifp->ifindex == ifindex) {
			return ifp;
		}
	}
	return NULL;
}

struct interface *if_lookup_by_index(ifindex_t ifindex) {
	return if_lookup_by_index_vrf(ifindex, VRF_DEFAULT);
}

const char *ifindex2ifname_vrf(ifindex_t ifindex, vrf_id_t vrf_id) {
	struct interface *ifp;

	return ((ifp = if_lookup_by_index_vrf(ifindex, vrf_id)) != NULL) ? ifp->name : "unknown";
}

const char *ifindex2ifname(ifindex_t ifindex) {
	return ifindex2ifname_vrf(ifindex, VRF_DEFAULT);
}

ifindex_t ifname2ifindex_vrf(const char *name, vrf_id_t vrf_id) {
	struct interface *ifp;

	return ((ifp = if_lookup_by_name_vrf(name, vrf_id)) != NULL) ? ifp->ifindex : IFINDEX_INTERNAL;
}

ifindex_t ifname2ifindex(const char *name) {
	return ifname2ifindex_vrf(name, VRF_DEFAULT);
}

/* Interface existance check by interface name. */
struct interface *if_lookup_by_name_vrf(const char *name, vrf_id_t vrf_id) {
	struct listnode *node;
	struct interface *ifp;

	if(name) {
		for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
			if(strcmp(name, ifp->name) == 0) {
				return ifp;
			}
		}
	}
	return NULL;
}

struct interface *if_lookup_by_name(const char *name) {
	return if_lookup_by_name_vrf(name, VRF_DEFAULT);
}

struct interface *if_lookup_by_name_len_vrf(const char *name, size_t namelen, vrf_id_t vrf_id) {
	struct listnode *node;
	struct interface *ifp;

	if(namelen > INTERFACE_NAMSIZ) {
		return NULL;
	}

	for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
		if(!memcmp(name, ifp->name, namelen) && (ifp->name[namelen] == '\0')) {
			return ifp;
		}
	}
	return NULL;
}

struct interface *if_lookup_by_name_len(const char *name, size_t namelen) {
	return if_lookup_by_name_len_vrf(name, namelen, VRF_DEFAULT);
}

/* Lookup interface by IPv4 address. */
struct interface *if_lookup_exact_address_vrf(struct in_addr src, vrf_id_t vrf_id) {
	struct listnode *node;
	struct listnode *cnode;
	struct interface *ifp;
	struct prefix *p;
	struct connected *c;

	for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
		for(ALL_LIST_ELEMENTS_RO(ifp->connected, cnode, c)) {
			p = c->address;

			if(p && p->family == AF_INET) {
				if(IPV4_ADDR_SAME(&p->u.prefix4, &src)) {
					return ifp;
				}
			}
		}
	}
	return NULL;
}

struct interface *if_lookup_exact_address(struct in_addr src) {
	return if_lookup_exact_address_vrf(src, VRF_DEFAULT);
}

/* Lookup interface by IPv4 address. */
struct interface *if_lookup_address_vrf(struct in_addr src, vrf_id_t vrf_id) {
	struct listnode *node;
	struct prefix addr;
	int bestlen = 0;
	struct listnode *cnode;
	struct interface *ifp;
	struct connected *c;
	struct interface *match;

	addr.family = AF_INET;
	addr.u.prefix4 = src;
	addr.prefixlen = IPV4_MAX_BITLEN;

	match = NULL;

	for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
		for(ALL_LIST_ELEMENTS_RO(ifp->connected, cnode, c)) {
			if(c->address && (c->address->family == AF_INET) && prefix_match(CONNECTED_PREFIX(c), &addr) && (c->address->prefixlen > bestlen)) {
				bestlen = c->address->prefixlen;
				match = ifp;
			}
		}
	}
	return match;
}

struct interface *if_lookup_address(struct in_addr src) {
	return if_lookup_address_vrf(src, VRF_DEFAULT);
}

/* Lookup interface by prefix */
struct interface *if_lookup_prefix_vrf(struct prefix *prefix, vrf_id_t vrf_id) {
	struct listnode *node;
	struct listnode *cnode;
	struct interface *ifp;
	struct connected *c;

	for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
		for(ALL_LIST_ELEMENTS_RO(ifp->connected, cnode, c)) {
			if(prefix_cmp(c->address, prefix) == 0) {
				return ifp;
			}
		}
	}
	return NULL;
}

struct interface *if_lookup_prefix(struct prefix *prefix) {
	return if_lookup_prefix_vrf(prefix, VRF_DEFAULT);
}

/* Get interface by name if given name interface doesn't exist create
   one. */
struct interface *if_get_by_name_vrf(const char *name, vrf_id_t vrf_id) {
	struct interface *ifp;

	return ((ifp = if_lookup_by_name_vrf(name, vrf_id)) != NULL) ? ifp : if_create_vrf(name, strlen(name), vrf_id);
}

struct interface *if_get_by_name(const char *name) {
	return if_get_by_name_vrf(name, VRF_DEFAULT);
}

struct interface *if_get_by_name_len_vrf(const char *name, size_t namelen, vrf_id_t vrf_id) {
	struct interface *ifp;

	return ((ifp = if_lookup_by_name_len_vrf(name, namelen, vrf_id)) != NULL) ? ifp : if_create_vrf(name, namelen, vrf_id);
}

struct interface *if_get_by_name_len(const char *name, size_t namelen) {
	return if_get_by_name_len_vrf(name, namelen, VRF_DEFAULT);
}

/* Does interface up ? */
int if_is_up(struct interface *ifp) {
	return ifp->flags & IFF_UP;
}

/* Is interface running? */
int if_is_running(struct interface *ifp) {
	return ifp->flags & IFF_RUNNING;
}

/* Is the interface operative, eg. either UP & RUNNING
   or UP & !ZEBRA_INTERFACE_LINK_DETECTION */
int if_is_operative(struct interface *ifp) {
	return ((ifp->flags & IFF_UP) && (ifp->flags & IFF_RUNNING || !CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION)));
}

/* Is this loopback interface ? */
int if_is_loopback(struct interface *ifp) {
	/* XXX: Do this better, eg what if IFF_WHATEVER means X on platform M
   * but Y on platform N?
   */
	return (ifp->flags & (IFF_LOOPBACK | IFF_NOXMIT | IFF_VIRTUAL));
}

/* Does this interface support broadcast ? */
int if_is_broadcast(struct interface *ifp) {
	return ifp->flags & IFF_BROADCAST;
}

/* Does this interface support broadcast ? */
int if_is_pointopoint(struct interface *ifp) {
	return ifp->flags & IFF_POINTOPOINT;
}

/* Does this interface support multicast ? */
int if_is_multicast(struct interface *ifp) {
	return ifp->flags & IFF_MULTICAST;
}

/* Printout flag information into log */
const char *if_flag_dump(unsigned long flag) {
	int separator = 0;
	static char logbuf[BUFSIZ];

#define IFF_OUT_LOG(X, STR) \
	if(flag & (X)) { \
		if(separator) strlcat(logbuf, ",", BUFSIZ); \
		else separator = 1; \
		strlcat(logbuf, STR, BUFSIZ); \
	}

	strlcpy(logbuf, "<", BUFSIZ);
	IFF_OUT_LOG(IFF_UP, "UP");
	IFF_OUT_LOG(IFF_BROADCAST, "BROADCAST");
	IFF_OUT_LOG(IFF_DEBUG, "DEBUG");
	IFF_OUT_LOG(IFF_LOOPBACK, "LOOPBACK");
	IFF_OUT_LOG(IFF_POINTOPOINT, "POINTOPOINT");
	IFF_OUT_LOG(IFF_NOTRAILERS, "NOTRAILERS");
	IFF_OUT_LOG(IFF_RUNNING, "RUNNING");
	IFF_OUT_LOG(IFF_NOARP, "NOARP");
	IFF_OUT_LOG(IFF_PROMISC, "PROMISC");
	IFF_OUT_LOG(IFF_ALLMULTI, "ALLMULTI");
	IFF_OUT_LOG(IFF_OACTIVE, "OACTIVE");
	IFF_OUT_LOG(IFF_SIMPLEX, "SIMPLEX");
	IFF_OUT_LOG(IFF_LINK0, "LINK0");
	IFF_OUT_LOG(IFF_LINK1, "LINK1");
	IFF_OUT_LOG(IFF_LINK2, "LINK2");
	IFF_OUT_LOG(IFF_MULTICAST, "MULTICAST");
	IFF_OUT_LOG(IFF_NOXMIT, "NOXMIT");
	IFF_OUT_LOG(IFF_NORTEXCH, "NORTEXCH");
	IFF_OUT_LOG(IFF_VIRTUAL, "VIRTUAL");
	IFF_OUT_LOG(IFF_IPV4, "IPv4");
	IFF_OUT_LOG(IFF_IPV6, "IPv6");

	strlcat(logbuf, ">", BUFSIZ);

	return logbuf;
#undef IFF_OUT_LOG
}

/* For debugging */
static void if_dump(const struct interface *ifp) {
	struct listnode *node;
	struct connected *c __attribute__((unused));

	for(ALL_LIST_ELEMENTS_RO(ifp->connected, node, c)) {
		zlog_info(
			"Interface %s vrf %u index %d metric %d mtu %d "
#ifdef HAVE_IPV6
			"mtu6 %d "
#endif /* HAVE_IPV6 */
			"%s",
			ifp->name, ifp->vrf_id, ifp->ifindex, ifp->metric, ifp->mtu,
#ifdef HAVE_IPV6
			ifp->mtu6,
#endif /* HAVE_IPV6 */
			if_flag_dump(ifp->flags)
		);
	}
}

/* Interface printing for all interface. */
void if_dump_all(void) {
	struct list *intf_list;
	struct listnode *node;
	void *p;
	vrf_iter_t iter;

	for(iter = vrf_first(); iter != VRF_ITER_INVALID; iter = vrf_next(iter)) {
		if((intf_list = vrf_iter2iflist(iter)) != NULL) {
			for(ALL_LIST_ELEMENTS_RO(intf_list, node, p)) {
				if_dump(p);
			}
		}
	}
}

DEFUN(interface_desc, interface_desc_cmd, "description .LINE",
      "Interface specific description\n"
      "Characters describing this interface\n") {
	struct interface *ifp;

	if(argc == 0) {
		return CMD_SUCCESS;
	}

	ifp = vty->index;
	if(ifp->desc) {
		XFREE(MTYPE_TMP, ifp->desc);
	}
	ifp->desc = argv_concat(argv, argc, 0);

	return CMD_SUCCESS;
}

DEFUN(no_interface_desc, no_interface_desc_cmd, "no description", NO_STR "Interface specific description\n") {
	struct interface *ifp;

	ifp = vty->index;
	if(ifp->desc) {
		XFREE(MTYPE_TMP, ifp->desc);
	}
	ifp->desc = NULL;

	return CMD_SUCCESS;
}

#ifdef SUNOS_5
/* Need to handle upgrade from SUNWzebra to Quagga. SUNWzebra created
 * a seperate struct interface for each logical interface, so config
 * file may be full of 'interface fooX:Y'. Solaris however does not
 * expose logical interfaces via PF_ROUTE, so trying to track logical
 * interfaces can be fruitless, for that reason Quagga only tracks
 * the primary IP interface.
 *
 * We try accomodate SUNWzebra by:
 * - looking up the interface name, to see whether it exists, if so
 *   its useable
 *   - for protocol daemons, this could only because zebra told us of
 *     the interface
 *   - for zebra, only because it learnt from kernel
 * - if not:
 *   - search the name to see if it contains a sub-ipif / logical interface
 *     seperator, the ':' char. If it does:
 *     - text up to that char must be the primary name - get that name.
 *     if not:
 *     - no idea, just get the name in its entirety.
 */
static struct interface *if_sunwzebra_get(const char *name, size_t nlen, vrf_id_t vrf_id) {
	struct interface *ifp;
	size_t seppos = 0;

	if((ifp = if_lookup_by_name_len_vrf(name, nlen, vrf_id)) != NULL) {
		return ifp;
	}

	/* hunt the primary interface name... */
	while(seppos < nlen && name[seppos] != ':') {
		seppos++;
	}

	/* Wont catch seperator as last char, e.g. 'foo0:' but thats invalid */
	if(seppos < nlen) {
		return if_get_by_name_len_vrf(name, seppos, vrf_id);
	} else {
		return if_get_by_name_len_vrf(name, nlen, vrf_id);
	}
}
#endif /* SUNOS_5 */

DEFUN(interface, interface_cmd, "interface IFNAME",
      "Select an interface to configure\n"
      "Interface's name\n") {
	struct interface *ifp;
	size_t sl;
	vrf_id_t vrf_id = VRF_DEFAULT;

	if((sl = strlen(argv[0])) > INTERFACE_NAMSIZ) {
		vty_out(vty,
			"%% Interface name %s is invalid: length exceeds "
			"%d characters%s",
			argv[0], INTERFACE_NAMSIZ, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if(argc > 1) {
		VTY_GET_INTEGER("VRF ID", vrf_id, argv[1]);
	}

#ifdef SUNOS_5
	ifp = if_sunwzebra_get(argv[0], sl, vrf_id);
#else
	ifp = if_get_by_name_len_vrf(argv[0], sl, vrf_id);
#endif /* SUNOS_5 */

	vty->index = ifp;
	vty->node = INTERFACE_NODE;

	return CMD_SUCCESS;
}

ALIAS(interface, interface_vrf_cmd, "interface IFNAME " VRF_CMD_STR,
      "Select an interface to configure\n"
      "Interface's name\n" VRF_CMD_HELP_STR)

DEFUN_NOSH(
	no_interface, no_interface_cmd, "no interface IFNAME",
	NO_STR "Delete a pseudo interface's configuration\n"
	       "Interface's name\n"
) {
	// deleting interface
	struct interface *ifp;
	vrf_id_t vrf_id = VRF_DEFAULT;

	if(argc > 1) {
		VTY_GET_INTEGER("VRF ID", vrf_id, argv[1]);
	}

	ifp = if_lookup_by_name_vrf(argv[0], vrf_id);

	if(ifp == NULL) {
		vty_out(vty, "%% Interface %s does not exist%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	if(CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_ACTIVE)) {
		vty_out(vty, "%% Only inactive interfaces can be deleted%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if_delete(ifp);

	return CMD_SUCCESS;
}

ALIAS(no_interface, no_interface_vrf_cmd, "no interface IFNAME " VRF_CMD_STR,
      NO_STR "Delete a pseudo interface's configuration\n"
	     "Interface's name\n" VRF_CMD_HELP_STR)

/* For debug purpose. */
DEFUN(show_address, show_address_cmd, "show address", SHOW_STR "address\n") {
	struct listnode *node;
	struct listnode *node2;
	struct interface *ifp;
	struct connected *ifc;
	struct prefix *p;
	vrf_id_t vrf_id = VRF_DEFAULT;

	if(argc > 0) {
		VTY_GET_INTEGER("VRF ID", vrf_id, argv[0]);
	}

	for(ALL_LIST_ELEMENTS_RO(vrf_iflist(vrf_id), node, ifp)) {
		for(ALL_LIST_ELEMENTS_RO(ifp->connected, node2, ifc)) {
			p = ifc->address;

			if(p->family == AF_INET) {
				vty_out(vty, "%s/%d%s", inet_ntoa(p->u.prefix4), p->prefixlen, VTY_NEWLINE);
			}
		}
	}
	return CMD_SUCCESS;
}

ALIAS(show_address, show_address_vrf_cmd, "show address " VRF_CMD_STR, SHOW_STR "address\n" VRF_CMD_HELP_STR)

DEFUN(show_address_vrf_all, show_address_vrf_all_cmd, "show address " VRF_ALL_CMD_STR, SHOW_STR "address\n" VRF_ALL_CMD_HELP_STR) {
	struct list *intf_list;
	struct listnode *node;
	struct listnode *node2;
	struct interface *ifp;
	struct connected *ifc;
	struct prefix *p;
	vrf_iter_t iter;

	for(iter = vrf_first(); iter != VRF_ITER_INVALID; iter = vrf_next(iter)) {
		intf_list = vrf_iter2iflist(iter);
		if(!intf_list || !listcount(intf_list)) {
			continue;
		}

		vty_out(vty, "%sVRF %u%s%s", VTY_NEWLINE, vrf_iter2id(iter), VTY_NEWLINE, VTY_NEWLINE);

		for(ALL_LIST_ELEMENTS_RO(intf_list, node, ifp)) {
			for(ALL_LIST_ELEMENTS_RO(ifp->connected, node2, ifc)) {
				p = ifc->address;

				if(p->family == AF_INET) {
					vty_out(vty, "%s/%d%s", inet_ntoa(p->u.prefix4), p->prefixlen, VTY_NEWLINE);
				}
			}
		}
	}
	return CMD_SUCCESS;
}

/* Allocate connected structure. */
struct connected *connected_new(void) {
	return XCALLOC(MTYPE_CONNECTED, sizeof(struct connected));
}

/* Free connected structure. */
void connected_free(struct connected *connected) {
	if(connected->address) {
		prefix_free(connected->address);
	}

	if(connected->destination) {
		prefix_free(connected->destination);
	}

	if(connected->label) {
		XFREE(MTYPE_CONNECTED_LABEL, connected->label);
	}

	XFREE(MTYPE_CONNECTED, connected);
}

/* Print if_addr structure. */
static void __attribute__((unused)) connected_log(struct connected *connected, char *str) {
	struct prefix *p;
	struct interface *ifp;
	char logbuf[BUFSIZ];
	char buf[BUFSIZ];

	ifp = connected->ifp;
	p = connected->address;

	snprintf(logbuf, BUFSIZ, "%s interface %s vrf %u %s %s/%d ", str, ifp->name, ifp->vrf_id, prefix_family_str(p), inet_ntop(p->family, &p->u.prefix, buf, BUFSIZ), p->prefixlen);

	p = connected->destination;
	if(p) {
		strncat(logbuf, inet_ntop(p->family, &p->u.prefix, buf, BUFSIZ), BUFSIZ - strlen(logbuf));
	}
	zlog(NULL, LOG_INFO, "%s", logbuf);
}

/* If two connected address has same prefix return 1. */
static int connected_same_prefix(struct prefix *p1, struct prefix *p2) {
	if(p1->family == p2->family) {
		if(p1->family == AF_INET && IPV4_ADDR_SAME(&p1->u.prefix4, &p2->u.prefix4)) {
			return 1;
		}
#ifdef HAVE_IPV6
		if(p1->family == AF_INET6 && IPV6_ADDR_SAME(&p1->u.prefix6, &p2->u.prefix6)) {
			return 1;
		}
#endif /* HAVE_IPV6 */
	}
	return 0;
}

struct connected *connected_delete_by_prefix(struct interface *ifp, struct prefix *p) {
	struct listnode *node;
	struct listnode *next;
	struct connected *ifc;

	/* In case of same prefix come, replace it with new one. */
	for(node = listhead(ifp->connected); node; node = next) {
		ifc = listgetdata(node);
		next = node->next;

		if(connected_same_prefix(ifc->address, p)) {
			listnode_delete(ifp->connected, ifc);
			return ifc;
		}
	}
	return NULL;
}

/* Find the IPv4 address on our side that will be used when packets
   are sent to dst. */
struct connected *connected_lookup_address(struct interface *ifp, struct in_addr dst) {
	struct prefix addr;
	struct listnode *cnode;
	struct connected *c;
	struct connected *match;

	addr.family = AF_INET;
	addr.u.prefix4 = dst;
	addr.prefixlen = IPV4_MAX_BITLEN;

	match = NULL;

	for(ALL_LIST_ELEMENTS_RO(ifp->connected, cnode, c)) {
		if(c->address && (c->address->family == AF_INET) && prefix_match(CONNECTED_PREFIX(c), &addr) && (!match || (c->address->prefixlen > match->address->prefixlen))) {
			match = c;
		}
	}
	return match;
}

struct connected *connected_add_by_prefix(struct interface *ifp, struct prefix *p, struct prefix *destination) {
	struct connected *ifc;

	/* Allocate new connected address. */
	ifc = connected_new();
	ifc->ifp = ifp;

	/* Fetch interface address */
	ifc->address = prefix_new();
	memcpy(ifc->address, p, sizeof(struct prefix));

	/* Fetch dest address */
	if(destination) {
		ifc->destination = prefix_new();
		memcpy(ifc->destination, destination, sizeof(struct prefix));
	}

	/* Add connected address to the interface. */
	listnode_add(ifp->connected, ifc);
	return ifc;
}

#ifndef HAVE_IF_NAMETOINDEX
ifindex_t if_nametoindex(const char *name) {
	struct interface *ifp;

	return ((ifp = if_lookup_by_name_len(name, strnlen(name, IFNAMSIZ))) != NULL) ? ifp->ifindex : 0;
}
#endif

#ifndef HAVE_IF_INDEXTONAME
char *if_indextoname(ifindex_t ifindex, char *name) {
	struct interface *ifp;

	if(!(ifp = if_lookup_by_index(ifindex))) {
		return NULL;
	}
	strncpy(name, ifp->name, IFNAMSIZ);
	return ifp->name;
}
#endif

#if 0  /* this route_table of struct connected's is unused
       * however, it would be good to use a route_table rather than
       * a list..
       */
/* Interface looking up by interface's address. */
/* Interface's IPv4 address reverse lookup table. */
struct route_table *ifaddr_ipv4_table;
/* struct route_table *ifaddr_ipv6_table; */

static void
ifaddr_ipv4_add (struct in_addr *ifaddr, struct interface *ifp)
{
  struct route_node *rn;
  struct prefix_ipv4 p;

  p.family = AF_INET;
  p.prefixlen = IPV4_MAX_PREFIXLEN;
  p.prefix = *ifaddr;

  rn = route_node_get (ifaddr_ipv4_table, (struct prefix *) &p);
  if (rn)
    {
      route_unlock_node (rn);
      zlog_info ("ifaddr_ipv4_add(): address %s is already added",
		 inet_ntoa (*ifaddr));
      return;
    }
  rn->info = ifp;
}

static void
ifaddr_ipv4_delete (struct in_addr *ifaddr, struct interface *ifp)
{
  struct route_node *rn;
  struct prefix_ipv4 p;

  p.family = AF_INET;
  p.prefixlen = IPV4_MAX_PREFIXLEN;
  p.prefix = *ifaddr;

  rn = route_node_lookup (ifaddr_ipv4_table, (struct prefix *) &p);
  if (! rn)
    {
      zlog_info ("ifaddr_ipv4_delete(): can't find address %s",
		 inet_ntoa (*ifaddr));
      return;
    }
  rn->info = NULL;
  route_unlock_node (rn);
  route_unlock_node (rn);
}

/* Lookup interface by interface's IP address or interface index. */
static struct interface *
ifaddr_ipv4_lookup (struct in_addr *addr, ifindex_t ifindex)
{
  struct prefix_ipv4 p;
  struct route_node *rn;
  struct interface *ifp;

  if (addr)
    {
      p.family = AF_INET;
      p.prefixlen = IPV4_MAX_PREFIXLEN;
      p.prefix = *addr;

      rn = route_node_lookup (ifaddr_ipv4_table, (struct prefix *) &p);
      if (! rn)
	return NULL;

      ifp = rn->info;
      route_unlock_node (rn);
      return ifp;
    }
  else
    return if_lookup_by_index(ifindex);
}
#endif /* ifaddr_ipv4_table */

/* Initialize interface list. */
void if_init(vrf_id_t vrf_id, struct list **intf_list) {
	*intf_list = list_new();
#if 0
  ifaddr_ipv4_table = route_table_init ();
#endif /* ifaddr_ipv4_table */

	(*intf_list)->cmp = (int (*)(void *, void *)) if_cmp_func;

	if(vrf_id == VRF_DEFAULT) {
		iflist = *intf_list;
	}
}

void if_terminate(vrf_id_t vrf_id, struct list **intf_list) {
	for(;;) {
		struct interface *ifp;

		ifp = listnode_head(*intf_list);
		if(ifp == NULL) {
			break;
		}

		if_delete(ifp);
	}

	list_delete(*intf_list);
	*intf_list = NULL;

	if(vrf_id == VRF_DEFAULT) {
		iflist = NULL;
	}
}

const char *if_link_type_str(enum zebra_link_type llt) {
	switch(llt) {
#define llts(T, S) \
	case(T): return (S)
		llts(ZEBRA_LLT_UNKNOWN, "Unknown");
		llts(ZEBRA_LLT_ETHER, "Ethernet");
		llts(ZEBRA_LLT_EETHER, "Experimental Ethernet");
		llts(ZEBRA_LLT_AX25, "AX.25 Level 2");
		llts(ZEBRA_LLT_PRONET, "PROnet token ring");
		llts(ZEBRA_LLT_IEEE802, "IEEE 802.2 Ethernet/TR/TB");
		llts(ZEBRA_LLT_ARCNET, "ARCnet");
		llts(ZEBRA_LLT_APPLETLK, "AppleTalk");
		llts(ZEBRA_LLT_DLCI, "Frame Relay DLCI");
		llts(ZEBRA_LLT_ATM, "ATM");
		llts(ZEBRA_LLT_METRICOM, "Metricom STRIP");
		llts(ZEBRA_LLT_IEEE1394, "IEEE 1394 IPv4");
		llts(ZEBRA_LLT_EUI64, "EUI-64");
		llts(ZEBRA_LLT_INFINIBAND, "InfiniBand");
		llts(ZEBRA_LLT_SLIP, "SLIP");
		llts(ZEBRA_LLT_CSLIP, "Compressed SLIP");
		llts(ZEBRA_LLT_SLIP6, "SLIPv6");
		llts(ZEBRA_LLT_CSLIP6, "Compressed SLIPv6");
		llts(ZEBRA_LLT_ROSE, "ROSE packet radio");
		llts(ZEBRA_LLT_X25, "CCITT X.25");
		llts(ZEBRA_LLT_PPP, "PPP");
		llts(ZEBRA_LLT_CHDLC, "Cisco HDLC");
		llts(ZEBRA_LLT_RAWHDLC, "Raw HDLC");
		llts(ZEBRA_LLT_LAPB, "LAPB");
		llts(ZEBRA_LLT_IPIP, "IPIP Tunnel");
		llts(ZEBRA_LLT_IPIP6, "IPIP6 Tunnel");
		llts(ZEBRA_LLT_FRAD, "FRAD");
		llts(ZEBRA_LLT_SKIP, "SKIP vif");
		llts(ZEBRA_LLT_LOOPBACK, "Loopback");
		llts(ZEBRA_LLT_LOCALTLK, "Localtalk");
		llts(ZEBRA_LLT_FDDI, "FDDI");
		llts(ZEBRA_LLT_SIT, "IPv6-in-IPv4 SIT");
		llts(ZEBRA_LLT_IPDDP, "IP-in-DDP tunnel");
		llts(ZEBRA_LLT_IPGRE, "GRE over IP");
		llts(ZEBRA_LLT_PIMREG, "PIMSM registration");
		llts(ZEBRA_LLT_HIPPI, "HiPPI");
		llts(ZEBRA_LLT_IRDA, "IrDA");
		llts(ZEBRA_LLT_FCPP, "Fibre-Channel PtP");
		llts(ZEBRA_LLT_FCAL, "Fibre-Channel Arbitrated Loop");
		llts(ZEBRA_LLT_FCPL, "Fibre-Channel Public Loop");
		llts(ZEBRA_LLT_FCFABRIC, "Fibre-Channel Fabric");
		llts(ZEBRA_LLT_IEEE802_TR, "IEEE 802.2 Token Ring");
		llts(ZEBRA_LLT_IEEE80211, "IEEE 802.11");
		llts(ZEBRA_LLT_IEEE80211_RADIOTAP, "IEEE 802.11 Radiotap");
		llts(ZEBRA_LLT_IEEE802154, "IEEE 802.15.4");
		llts(ZEBRA_LLT_IEEE802154_PHY, "IEEE 802.15.4 Phy");
		default: zlog_warn("Unknown value %d", llt); return "Unknown type!";
#undef llts
	}
	return NULL;
}

struct if_link_params *if_link_params_get(struct interface *ifp) {
	int i;

	if(ifp->link_params != NULL) {
		return ifp->link_params;
	}

	struct if_link_params *iflp = XCALLOC(MTYPE_IF_LINK_PARAMS, sizeof(struct if_link_params));
	if(iflp == NULL) {
		return NULL;
	}

	/* Set TE metric == standard metric */
	iflp->te_metric = ifp->metric;

	/* Compute default bandwidth based on interface */
	int bw = (float) ((ifp->bandwidth ? ifp->bandwidth : DEFAULT_BANDWIDTH) * TE_KILO_BIT / TE_BYTE);

	/* Set Max, Reservable and Unreserved Bandwidth */
	iflp->max_bw = bw;
	iflp->max_rsv_bw = bw;
	for(i = 0; i < MAX_CLASS_TYPE; i++) {
		iflp->unrsv_bw[i] = bw;
	}

	/* Update Link parameters status */
	iflp->lp_status = LP_TE | LP_MAX_BW | LP_MAX_RSV_BW | LP_UNRSV_BW;

	/* Finally attach newly created Link Parameters */
	ifp->link_params = iflp;

	return iflp;
}

void if_link_params_free(struct interface *ifp) {
	if(ifp->link_params == NULL) {
		return;
	}
	XFREE(MTYPE_IF_LINK_PARAMS, ifp->link_params);
	ifp->link_params = NULL;
}
