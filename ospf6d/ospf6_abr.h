/*
 * Copyright (C) 2004 Yasuhiro Ohara
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
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

#ifndef OSPF6_ABR_H
#define OSPF6_ABR_H

/* for struct ospf6_route */
#include "ospf6_route.h"
/* for struct ospf6_prefix */
#include "ospf6_proto.h"

/* Debug option */
extern unsigned char conf_debug_ospf6_abr;
#define OSPF6_DEBUG_ABR_ON() (conf_debug_ospf6_abr = 1)
#define OSPF6_DEBUG_ABR_OFF() (conf_debug_ospf6_abr = 0)
#define IS_OSPF6_DEBUG_ABR (conf_debug_ospf6_abr)

/* Inter-Area-Prefix-LSA */
#define OSPF6_INTER_PREFIX_LSA_MIN_SIZE 4U /* w/o IPv6 prefix */

struct ospf6_inter_prefix_lsa {
	u_int32_t metric;
	struct ospf6_prefix prefix;
};

/* Inter-Area-Router-LSA */
#define OSPF6_INTER_ROUTER_LSA_FIX_SIZE 12U

struct ospf6_inter_router_lsa {
	u_char mbz;
	u_char options[3];
	u_int32_t metric;
	u_int32_t router_id;
};

#define OSPF6_ABR_SUMMARY_METRIC(E) (ntohl((E)->metric & htonl(0x00ffffff)))
#define OSPF6_ABR_SUMMARY_METRIC_SET(E, C) \
	{ \
		(E)->metric &= htonl(0x00000000); \
		(E)->metric |= htonl(0x00ffffff) & htonl(C); \
	}

extern int ospf6_is_router_abr(struct ospf6 *o);

extern void ospf6_abr_enable_area(struct ospf6_area *oa);
extern void ospf6_abr_disable_area(struct ospf6_area *oa);

extern void ospf6_abr_originate_summary_to_area(struct ospf6_route *route, struct ospf6_area *area);
extern void ospf6_abr_originate_summary(struct ospf6_route *route);
extern void ospf6_abr_examin_summary(struct ospf6_lsa *lsa, struct ospf6_area *oa);
extern void ospf6_abr_examin_brouter(u_int32_t router_id);
extern void ospf6_abr_reimport(struct ospf6_area *oa);

extern int config_write_ospf6_debug_abr(struct vty *vty);
extern void install_element_ospf6_debug_abr(void);
extern int ospf6_abr_config_write(struct vty *vty);

extern void ospf6_abr_init(void);

#endif /*OSPF6_ABR_H*/
