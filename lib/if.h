/* Interface related header.
   Copyright (C) 1997, 98, 99 Kunihiro Ishiguro
   (C)2024-2025 Hikaru Yamatohimemiya

This file is part of GNU Zebra.

GNU Zebra is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2, or (at your
option) any later version.

GNU Zebra is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Zebra; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#ifndef _ZEBRA_IF_H
#define _ZEBRA_IF_H

#include "linklist.h"
#include "zebra.h"

/* Interface link-layer type, if known. Derived from:
 *
 * net/if_arp.h on various platforms - Linux especially.
 * http://www.iana.org/assignments/arp-parameters/arp-parameters.xhtml
 *
 * Some of the more obviously defunct technologies left out.
 */
enum zebra_link_type {
	ZEBRA_LLT_UNKNOWN = 0,
	ZEBRA_LLT_ETHER,
	ZEBRA_LLT_EETHER,
	ZEBRA_LLT_AX25,
	ZEBRA_LLT_PRONET,
	ZEBRA_LLT_IEEE802,
	ZEBRA_LLT_ARCNET,
	ZEBRA_LLT_APPLETLK,
	ZEBRA_LLT_DLCI,
	ZEBRA_LLT_ATM,
	ZEBRA_LLT_METRICOM,
	ZEBRA_LLT_IEEE1394,
	ZEBRA_LLT_EUI64,
	ZEBRA_LLT_INFINIBAND,
	ZEBRA_LLT_SLIP,
	ZEBRA_LLT_CSLIP,
	ZEBRA_LLT_SLIP6,
	ZEBRA_LLT_CSLIP6,
	ZEBRA_LLT_RSRVD,
	ZEBRA_LLT_ADAPT,
	ZEBRA_LLT_ROSE,
	ZEBRA_LLT_X25,
	ZEBRA_LLT_PPP,
	ZEBRA_LLT_CHDLC,
	ZEBRA_LLT_LAPB,
	ZEBRA_LLT_RAWHDLC,
	ZEBRA_LLT_IPIP,
	ZEBRA_LLT_IPIP6,
	ZEBRA_LLT_FRAD,
	ZEBRA_LLT_SKIP,
	ZEBRA_LLT_LOOPBACK,
	ZEBRA_LLT_LOCALTLK,
	ZEBRA_LLT_FDDI,
	ZEBRA_LLT_SIT,
	ZEBRA_LLT_IPDDP,
	ZEBRA_LLT_IPGRE,
	ZEBRA_LLT_IP6GRE,
	ZEBRA_LLT_PIMREG,
	ZEBRA_LLT_HIPPI,
	ZEBRA_LLT_ECONET,
	ZEBRA_LLT_IRDA,
	ZEBRA_LLT_FCPP,
	ZEBRA_LLT_FCAL,
	ZEBRA_LLT_FCPL,
	ZEBRA_LLT_FCFABRIC,
	ZEBRA_LLT_IEEE802_TR,
	ZEBRA_LLT_IEEE80211,
	ZEBRA_LLT_IEEE80211_RADIOTAP,
	ZEBRA_LLT_IEEE802154,
	ZEBRA_LLT_IEEE802154_PHY,
};

/*
  Interface name length.

   Linux define value in /usr/include/linux/if.h.
   #define IFNAMSIZ        16

   FreeBSD define value in /usr/include/net/if.h.
   #define IFNAMSIZ        16
*/

#define INTERFACE_NAMSIZ 20
#define INTERFACE_HWADDR_MAX 20

typedef signed int ifindex_t;

#ifdef HAVE_PROC_NET_DEV
struct if_stats {
	unsigned long rx_packets;   /* total packets received       */
	unsigned long tx_packets;   /* total packets transmitted    */
	unsigned long rx_bytes;	    /* total bytes received         */
	unsigned long tx_bytes;	    /* total bytes transmitted      */
	unsigned long rx_errors;    /* bad packets received         */
	unsigned long tx_errors;    /* packet transmit problems     */
	unsigned long rx_dropped;   /* no space in linux buffers    */
	unsigned long tx_dropped;   /* no space available in linux  */
	unsigned long rx_multicast; /* multicast packets received   */
	unsigned long rx_compressed;
	unsigned long tx_compressed;
	unsigned long collisions;

	/* detailed rx_errors: */
	unsigned long rx_length_errors;
	unsigned long rx_over_errors;	/* receiver ring buff overflow  */
	unsigned long rx_crc_errors;	/* recved pkt with crc error    */
	unsigned long rx_frame_errors;	/* recv'd frame alignment error */
	unsigned long rx_fifo_errors;	/* recv'r fifo overrun          */
	unsigned long rx_missed_errors; /* receiver missed packet     */
	/* detailed tx_errors */
	unsigned long tx_aborted_errors;
	unsigned long tx_carrier_errors;
	unsigned long tx_fifo_errors;
	unsigned long tx_heartbeat_errors;
	unsigned long tx_window_errors;
};
#endif /* HAVE_PROC_NET_DEV */

/* Here are "non-official" architectural constants. */
#define TE_EXT_MASK 0x0FFFFFFF
#define TE_EXT_ANORMAL 0x80000000
#define LOSS_PRECISION 0.000003
#define TE_KILO_BIT 1000
#define TE_BYTE 8
#define DEFAULT_BANDWIDTH 10000
#define MAX_CLASS_TYPE 8
#define MAX_PKT_LOSS 50.331642

/* Link Parameters Status: 0: unset, 1: set, */
#define LP_UNSET 0x0000
#define LP_TE 0x0001
#define LP_MAX_BW 0x0002
#define LP_MAX_RSV_BW 0x0004
#define LP_UNRSV_BW 0x0008
#define LP_ADM_GRP 0x0010
#define LP_RMT_AS 0x0020
#define LP_DELAY 0x0040
#define LP_MM_DELAY 0x0080
#define LP_DELAY_VAR 0x0100
#define LP_PKT_LOSS 0x0200
#define LP_RES_BW 0x0400
#define LP_AVA_BW 0x0800
#define LP_USE_BW 0x1000

#define IS_PARAM_UNSET(lp, st) !(lp->lp_status & st)
#define IS_PARAM_SET(lp, st) (lp->lp_status & st)
#define IS_LINK_PARAMS_SET(lp) (lp->lp_status != LP_UNSET)

#define SET_PARAM(lp, st) (lp->lp_status) |= (st)
#define UNSET_PARAM(lp, st) (lp->lp_status) &= ~(st)
#define RESET_LINK_PARAM(lp) (lp->lp_status = LP_UNSET)

/* Link Parameters for Traffic Engineering */
struct if_link_params {
	u_int32_t lp_status;		/* Status of Link Parameters: */
	u_int32_t te_metric;		/* Traffic Engineering metric */
	float max_bw;			/* Maximum Bandwidth */
	float max_rsv_bw;		/* Maximum Reservable Bandwidth */
	float unrsv_bw[MAX_CLASS_TYPE]; /* Unreserved Bandwidth per Class Type (8) */
	u_int32_t admin_grp;		/* Administrative group */
	u_int32_t rmt_as;		/* Remote AS number */
	struct in_addr rmt_ip;		/* Remote IP address */
	u_int32_t av_delay;		/* Link Average Delay */
	u_int32_t min_delay;		/* Link Min Delay */
	u_int32_t max_delay;		/* Link Max Delay */
	u_int32_t delay_var;		/* Link Delay Variation */
	float pkt_loss;			/* Link Packet Loss */
	float res_bw;			/* Residual Bandwidth */
	float ava_bw;			/* Available Bandwidth */
	float use_bw;			/* Utilized Bandwidth */
};

#define INTERFACE_LINK_PARAMS_SIZE sizeof(struct if_link_params)
#define HAS_LINK_PARAMS(ifp) ((ifp)->link_params != NULL)

/* Interface structure */
struct interface {
	/* Interface name.  This should probably never be changed after the
     interface is created, because the configuration info for this interface
     is associated with this structure.  For that reason, the interface
     should also never be deleted (to avoid losing configuration info).
     To delete, just set ifindex to IFINDEX_INTERNAL to indicate that the
     interface does not exist in the kernel.
   */
	char name[INTERFACE_NAMSIZ + 1];

	/* Interface index (should be IFINDEX_INTERNAL for non-kernel or
     deleted interfaces). */
	ifindex_t ifindex;
#define IFINDEX_INTERNAL 0

	/* Zebra internal interface status */
	u_char status;
#define ZEBRA_INTERFACE_ACTIVE (1 << 0)
#define ZEBRA_INTERFACE_SUB (1 << 1)
#define ZEBRA_INTERFACE_LINKDETECTION (1 << 2)

	/* Interface flags. */
	uint64_t flags;

	/* Interface metric */
	int metric;

	/* Interface MTU. */
	unsigned int mtu;  /* IPv4 MTU */
	unsigned int mtu6; /* IPv6 MTU - probably, but not neccessarily same as mtu */

	/* Link-layer information and hardware address */
	enum zebra_link_type ll_type;
	u_char hw_addr[INTERFACE_HWADDR_MAX];
	int hw_addr_len;

	/* interface bandwidth, kbits */
	unsigned int bandwidth;

	/* Link parameters for Traffic Engineering */
	struct if_link_params *link_params;

	/* description of the interface. */
	char *desc;

	/* Distribute list. */
	void *distribute_in;
	void *distribute_out;

	/* Connected address list. */
	struct list *connected;

	/* Daemon specific interface data pointer. */
	void *info;

	/* Statistics fileds. */
#ifdef HAVE_PROC_NET_DEV
	struct if_stats stats;
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
	struct if_data stats;
#endif /* HAVE_NET_RT_IFLIST */

	vrf_id_t vrf_id;
};

/* Connected address structure. */
struct connected {
	/* Attached interface. */
	struct interface *ifp;

	/* Flags for configuration. */
	u_char conf;
#define ZEBRA_IFC_REAL (1 << 0)
#define ZEBRA_IFC_CONFIGURED (1 << 1)
#define ZEBRA_IFC_QUEUED (1 << 2)
	/*
     The ZEBRA_IFC_REAL flag should be set if and only if this address
     exists in the kernel and is actually usable. (A case where it exists but
     is not yet usable would be IPv6 with DAD)
     The ZEBRA_IFC_CONFIGURED flag should be set if and only if this address
     was configured by the user from inside quagga.
     The ZEBRA_IFC_QUEUED flag should be set if and only if the address exists
     in the kernel. It may and should be set although the address might not be
     usable yet. (compare with ZEBRA_IFC_REAL)
   */

	/* Flags for connected address. */
	u_char flags;
#define ZEBRA_IFA_SECONDARY (1 << 0)
#define ZEBRA_IFA_PEER (1 << 1)
#define ZEBRA_IFA_UNNUMBERED (1 << 2)
	/* N.B. the ZEBRA_IFA_PEER flag should be set if and only if
     a peer address has been configured.  If this flag is set,
     the destination field must contain the peer address.
     Otherwise, if this flag is not set, the destination address
     will either contain a broadcast address or be NULL.
   */

	/* Address of connected network. */
	struct prefix *address;

	/* Peer or Broadcast address, depending on whether ZEBRA_IFA_PEER is set.
     Note: destination may be NULL if ZEBRA_IFA_PEER is not set. */
	struct prefix *destination;

	/* Label for Linux 2.2.X and upper. */
	char *label;
};

/* Does the destination field contain a peer address? */
#define CONNECTED_PEER(C) CHECK_FLAG((C)->flags, ZEBRA_IFA_PEER)

/* Prefix to insert into the RIB */
#define CONNECTED_PREFIX(C) (CONNECTED_PEER(C) ? (C)->destination : (C)->address)

/* Identifying address.  We guess that if there's a peer address, but the
   local address is in the same prefix, then the local address may be unique. */
#define CONNECTED_ID(C) ((CONNECTED_PEER(C) && !prefix_match((C)->destination, (C)->address)) ? (C)->destination : (C)->address)

/* Interface hook sort. */
#define IF_NEW_HOOK 0
#define IF_DELETE_HOOK 1

/* There are some interface flags which are only supported by some
   operating system. */

#ifndef IFF_NOTRAILERS
	#define IFF_NOTRAILERS 0x0
#endif /* IFF_NOTRAILERS */
#ifndef IFF_OACTIVE
	#define IFF_OACTIVE 0x0
#endif /* IFF_OACTIVE */
#ifndef IFF_SIMPLEX
	#define IFF_SIMPLEX 0x0
#endif /* IFF_SIMPLEX */
#ifndef IFF_LINK0
	#define IFF_LINK0 0x0
#endif /* IFF_LINK0 */
#ifndef IFF_LINK1
	#define IFF_LINK1 0x0
#endif /* IFF_LINK1 */
#ifndef IFF_LINK2
	#define IFF_LINK2 0x0
#endif /* IFF_LINK2 */
#ifndef IFF_NOXMIT
	#define IFF_NOXMIT 0x0
#endif /* IFF_NOXMIT */
#ifndef IFF_NORTEXCH
	#define IFF_NORTEXCH 0x0
#endif /* IFF_NORTEXCH */
#ifndef IFF_IPV4
	#define IFF_IPV4 0x0
#endif /* IFF_IPV4 */
#ifndef IFF_IPV6
	#define IFF_IPV6 0x0
#endif /* IFF_IPV6 */
#ifndef IFF_VIRTUAL
	#define IFF_VIRTUAL 0x0
#endif /* IFF_VIRTUAL */

/* Prototypes. */
extern int if_cmp_func(struct interface *, struct interface *);
extern struct interface *if_create(const char *name, int namelen);
extern struct interface *if_lookup_by_index(ifindex_t);
extern struct interface *if_lookup_exact_address(struct in_addr);
extern struct interface *if_lookup_address(struct in_addr);
extern struct interface *if_lookup_prefix(struct prefix *prefix);

extern struct interface *if_create_vrf(const char *name, int namelen, vrf_id_t vrf_id);
extern struct interface *if_lookup_by_index_vrf(ifindex_t, vrf_id_t vrf_id);
extern struct interface *if_lookup_exact_address_vrf(struct in_addr, vrf_id_t vrf_id);
extern struct interface *if_lookup_address_vrf(struct in_addr, vrf_id_t vrf_id);
extern struct interface *if_lookup_prefix_vrf(struct prefix *prefix, vrf_id_t vrf_id);

/* These 2 functions are to be used when the ifname argument is terminated
   by a '\0' character: */
extern struct interface *if_lookup_by_name(const char *ifname);
extern struct interface *if_get_by_name(const char *ifname);

extern struct interface *if_lookup_by_name_vrf(const char *ifname, vrf_id_t vrf_id);
extern struct interface *if_get_by_name_vrf(const char *ifname, vrf_id_t vrf_id);

/* For these 2 functions, the namelen argument should be the precise length
   of the ifname string (not counting any optional trailing '\0' character).
   In most cases, strnlen should be used to calculate the namelen value. */
extern struct interface *if_lookup_by_name_len(const char *ifname, size_t namelen);
extern struct interface *if_get_by_name_len(const char *ifname, size_t namelen);

extern struct interface *if_lookup_by_name_len_vrf(const char *ifname, size_t namelen, vrf_id_t vrf_id);
extern struct interface *if_get_by_name_len_vrf(const char *ifname, size_t namelen, vrf_id_t vrf_id);

/* Delete the interface, but do not free the structure, and leave it in the
   interface list.  It is often advisable to leave the pseudo interface
   structure because there may be configuration information attached. */
extern void if_delete_retain(struct interface *);

/* Delete and free the interface structure: calls if_delete_retain and then
   deletes it from the interface list and frees the structure. */
extern void if_delete(struct interface *);

extern int if_is_up(struct interface *);
extern int if_is_running(struct interface *);
extern int if_is_operative(struct interface *);
extern int if_is_loopback(struct interface *);
extern int if_is_broadcast(struct interface *);
extern int if_is_pointopoint(struct interface *);
extern int if_is_multicast(struct interface *);
extern void if_add_hook(int, int (*)(struct interface *));
extern void if_init(vrf_id_t, struct list **);
extern void if_terminate(vrf_id_t, struct list **);
extern void if_dump_all(void);
extern const char *if_flag_dump(unsigned long);
extern const char *if_link_type_str(enum zebra_link_type);

/* Please use ifindex2ifname instead of if_indextoname where possible;
   ifindex2ifname uses internal interface info, whereas if_indextoname must
   make a system call. */
extern const char *ifindex2ifname(ifindex_t);
extern const char *ifindex2ifname_vrf(ifindex_t, vrf_id_t vrf_id);

/* Please use ifname2ifindex instead of if_nametoindex where possible;
   ifname2ifindex uses internal interface info, whereas if_nametoindex must
   make a system call. */
extern ifindex_t ifname2ifindex(const char *ifname);
extern ifindex_t ifname2ifindex_vrf(const char *ifname, vrf_id_t vrf_id);

/* Connected address functions. */
extern struct connected *connected_new(void);
extern void connected_free(struct connected *);
extern void connected_add(struct interface *, struct connected *);
extern struct connected *connected_add_by_prefix(struct interface *, struct prefix *, struct prefix *);
extern struct connected *connected_delete_by_prefix(struct interface *, struct prefix *);
extern struct connected *connected_lookup_address(struct interface *, struct in_addr);

#ifndef HAVE_IF_NAMETOINDEX
extern ifindex_t if_nametoindex(const char *);
#endif
#ifndef HAVE_IF_INDEXTONAME
extern char *if_indextoname(ifindex_t, char *);
#endif

/* link parameters */
struct if_link_params *if_link_params_get(struct interface *);
void if_link_params_free(struct interface *);

/* Exported variables. */
extern struct list *iflist;
extern struct cmd_element interface_desc_cmd;
extern struct cmd_element no_interface_desc_cmd;
extern struct cmd_element interface_cmd;
extern struct cmd_element no_interface_cmd;
extern struct cmd_element interface_vrf_cmd;
extern struct cmd_element no_interface_vrf_cmd;
extern struct cmd_element interface_pseudo_cmd;
extern struct cmd_element no_interface_pseudo_cmd;
extern struct cmd_element show_address_cmd;
extern struct cmd_element show_address_vrf_cmd;
extern struct cmd_element show_address_vrf_all_cmd;

#endif /* _ZEBRA_IF_H */
