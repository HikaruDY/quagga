/*
  PIM for Quagga: add the ability to configure multicast static routes
  Copyright (C) 2014  Nathan Bahr, ATCorp

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING; if not, write to the
  Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
  MA 02110-1301 USA

  $QuaggaId: $Format:%an, %ai, %h$ $
*/

#include <zebra.h>

#include "vty.h"

#include "pim_static.h"
#include "pim_time.h"
#include "pim_str.h"
#include "pimd.h"
#include "pim_iface.h"
#include "log.h"
#include "memory.h"
#include "linklist.h"

void pim_static_route_free(struct static_route *s_route) {
	XFREE(MTYPE_PIM_STATIC_ROUTE, s_route);
}

static struct static_route *static_route_alloc() {
	struct static_route *s_route;

	s_route = XCALLOC(MTYPE_PIM_STATIC_ROUTE, sizeof(*s_route));
	if(!s_route) {
		zlog_err("PIM XCALLOC(%zu) failure", sizeof(*s_route));
		return 0;
	}
	return s_route;
}

static struct static_route *static_route_new(unsigned int iif, unsigned int oif, struct in_addr group, struct in_addr source) {
	struct static_route *s_route;
	s_route = static_route_alloc();
	if(!s_route) {
		return 0;
	}

	s_route->group = group;
	s_route->source = source;
	s_route->iif = iif;
	s_route->oif_ttls[oif] = 1;
	s_route->oif_count = 1;
	s_route->mc.mfcc_origin = source;
	s_route->mc.mfcc_mcastgrp = group;
	s_route->mc.mfcc_parent = iif;
	s_route->mc.mfcc_ttls[oif] = 1;
	s_route->creation[oif] = pim_time_monotonic_sec();

	return s_route;
}

int pim_static_add(struct interface *iif, struct interface *oif, struct in_addr group, struct in_addr source) {
	struct listnode *node = 0;
	struct static_route *s_route = 0;
	struct static_route *original_s_route = 0;
	struct pim_interface *pim_iif = iif ? iif->info : 0;
	struct pim_interface *pim_oif = oif ? oif->info : 0;
	unsigned int iif_index = pim_iif ? pim_iif->mroute_vif_index : 0;
	unsigned int oif_index = pim_oif ? pim_oif->mroute_vif_index : 0;

	if(!iif_index || !oif_index) {
		zlog_warn("%s %s: Unable to add static route: Invalid interface index(iif=%d,oif=%d)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index);
		return -2;
	}

#ifdef PIM_ENFORCE_LOOPFREE_MFC
	if(iif_index == oif_index) {
		/* looped MFC entry */
		zlog_warn("%s %s: Unable to add static route: Looped MFC entry(iif=%d,oif=%d)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index);
		return -4;
	}
#endif

	for(ALL_LIST_ELEMENTS_RO(qpim_static_route_list, node, s_route)) {
		if(s_route->group.s_addr == group.s_addr && s_route->source.s_addr == source.s_addr) {
			if(s_route->iif == iif_index && s_route->oif_ttls[oif_index]) {
				char gifaddr_str[100];
				char sifaddr_str[100];
				pim_inet4_dump("<ifaddr?>", group, gifaddr_str, sizeof(gifaddr_str));
				pim_inet4_dump("<ifaddr?>", source, sifaddr_str, sizeof(sifaddr_str));
				zlog_warn("%s %s: Unable to add static route: Route already exists (iif=%d,oif=%d,group=%s,source=%s)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index, gifaddr_str, sifaddr_str);
				return -3;
			}

			/* Ok, from here on out we will be making changes to the s_route structure, but if
          * for some reason we fail to commit these changes to the kernel, we want to be able
          * restore the state of the list. So copy the node data and if need be, we can copy
          * back if it fails.
          */
			original_s_route = static_route_alloc();
			if(!original_s_route) {
				return -5;
			}
			memcpy(original_s_route, s_route, sizeof(struct static_route));

			/* Route exists and has the same input interface, but adding a new output interface */
			if(s_route->iif == iif_index) {
				s_route->oif_ttls[oif_index] = 1;
				s_route->mc.mfcc_ttls[oif_index] = 1;
				s_route->creation[oif_index] = pim_time_monotonic_sec();
				++s_route->oif_count;
			} else {
				/* input interface changed */
				s_route->iif = iif_index;
				s_route->mc.mfcc_parent = iif_index;

#ifdef PIM_ENFORCE_LOOPFREE_MFC
				/* check to make sure the new input was not an old output */
				if(s_route->oif_ttls[iif_index]) {
					s_route->oif_ttls[iif_index] = 0;
					s_route->creation[iif_index] = 0;
					s_route->mc.mfcc_ttls[iif_index] = 0;
					--s_route->oif_count;
				}
#endif

				/* now add the new output, if it is new */
				if(!s_route->oif_ttls[oif_index]) {
					s_route->oif_ttls[oif_index] = 1;
					s_route->creation[oif_index] = pim_time_monotonic_sec();
					s_route->mc.mfcc_ttls[oif_index] = 1;
					++s_route->oif_count;
				}
			}

			break;
		}
	}

	/* If node is null then we reached the end of the list without finding a match */
	if(!node) {
		s_route = static_route_new(iif_index, oif_index, group, source);
		listnode_add(qpim_static_route_list, s_route);
	}

	if(pim_mroute_add(&(s_route->mc))) {
		char gifaddr_str[100];
		char sifaddr_str[100];
		pim_inet4_dump("<ifaddr?>", group, gifaddr_str, sizeof(gifaddr_str));
		pim_inet4_dump("<ifaddr?>", source, sifaddr_str, sizeof(sifaddr_str));
		zlog_warn("%s %s: Unable to add static route(iif=%d,oif=%d,group=%s,source=%s)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index, gifaddr_str, sifaddr_str);

		/* Need to put s_route back to the way it was */
		if(original_s_route) {
			memcpy(s_route, original_s_route, sizeof(struct static_route));
		} else {
			/* we never stored off a copy, so it must have been a fresh new route */
			listnode_delete(qpim_static_route_list, s_route);
			pim_static_route_free(s_route);
		}

		if(original_s_route) {
			pim_static_route_free(original_s_route);
		}

		return -1;
	}

	/* Make sure we free the memory for the route copy if used */
	if(original_s_route) {
		pim_static_route_free(original_s_route);
	}

	if(PIM_DEBUG_STATIC) {
		char gifaddr_str[100];
		char sifaddr_str[100];
		pim_inet4_dump("<ifaddr?>", group, gifaddr_str, sizeof(gifaddr_str));
		pim_inet4_dump("<ifaddr?>", source, sifaddr_str, sizeof(sifaddr_str));
		zlog_debug("%s: Static route added(iif=%d,oif=%d,group=%s,source=%s)", __PRETTY_FUNCTION__, iif_index, oif_index, gifaddr_str, sifaddr_str);
	}

	return 0;
}

int pim_static_del(struct interface *iif, struct interface *oif, struct in_addr group, struct in_addr source) {
	struct listnode *node = 0;
	struct listnode *nextnode = 0;
	struct static_route *s_route = 0;
	struct pim_interface *pim_iif = iif ? iif->info : 0;
	struct pim_interface *pim_oif = oif ? oif->info : 0;
	unsigned int iif_index = pim_iif ? pim_iif->mroute_vif_index : 0;
	unsigned int oif_index = pim_oif ? pim_oif->mroute_vif_index : 0;

	if(!iif_index || !oif_index) {
		zlog_warn("%s %s: Unable to remove static route: Invalid interface index(iif=%d,oif=%d)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index);
		return -2;
	}

	for(ALL_LIST_ELEMENTS(qpim_static_route_list, node, nextnode, s_route)) {
		if(s_route->iif == iif_index && s_route->group.s_addr == group.s_addr && s_route->source.s_addr == source.s_addr && s_route->oif_ttls[oif_index]) {
			s_route->oif_ttls[oif_index] = 0;
			s_route->mc.mfcc_ttls[oif_index] = 0;
			--s_route->oif_count;

			/* If there are no more outputs then delete the whole route, otherwise set the route with the new outputs */
			if(s_route->oif_count <= 0 ? pim_mroute_del(&s_route->mc) : pim_mroute_add(&s_route->mc)) {
				char gifaddr_str[100];
				char sifaddr_str[100];
				pim_inet4_dump("<ifaddr?>", group, gifaddr_str, sizeof(gifaddr_str));
				pim_inet4_dump("<ifaddr?>", source, sifaddr_str, sizeof(sifaddr_str));
				zlog_warn("%s %s: Unable to remove static route(iif=%d,oif=%d,group=%s,source=%s)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index, gifaddr_str, sifaddr_str);

				s_route->oif_ttls[oif_index] = 1;
				s_route->mc.mfcc_ttls[oif_index] = 1;
				++s_route->oif_count;

				return -1;
			}

			s_route->creation[oif_index] = 0;

			if(s_route->oif_count <= 0) {
				listnode_delete(qpim_static_route_list, s_route);
				pim_static_route_free(s_route);
			}

			if(PIM_DEBUG_STATIC) {
				char gifaddr_str[100];
				char sifaddr_str[100];
				pim_inet4_dump("<ifaddr?>", group, gifaddr_str, sizeof(gifaddr_str));
				pim_inet4_dump("<ifaddr?>", source, sifaddr_str, sizeof(sifaddr_str));
				zlog_debug("%s: Static route removed(iif=%d,oif=%d,group=%s,source=%s)", __PRETTY_FUNCTION__, iif_index, oif_index, gifaddr_str, sifaddr_str);
			}

			break;
		}
	}

	if(!node) {
		char gifaddr_str[100];
		char sifaddr_str[100];
		pim_inet4_dump("<ifaddr?>", group, gifaddr_str, sizeof(gifaddr_str));
		pim_inet4_dump("<ifaddr?>", source, sifaddr_str, sizeof(sifaddr_str));
		zlog_warn("%s %s: Unable to remove static route: Route does not exist(iif=%d,oif=%d,group=%s,source=%s)", __FILE__, __PRETTY_FUNCTION__, iif_index, oif_index, gifaddr_str, sifaddr_str);
		return -3;
	}

	return 0;
}

int pim_static_write_mroute(struct vty *vty, struct interface *ifp) {
	struct listnode *node;
	struct static_route *sroute;
	int count = 0;
	char sbuf[100];
	char gbuf[100];

	for(ALL_LIST_ELEMENTS_RO(qpim_static_route_list, node, sroute)) {
		pim_inet4_dump("<ifaddr?>", sroute->group, gbuf, sizeof(gbuf));
		pim_inet4_dump("<ifaddr?>", sroute->source, sbuf, sizeof(sbuf));
		if(sroute->iif == (unsigned int) ifp->ifindex) {
			int i;
			for(i = 0; i < MAXVIFS; i++) {
				if(sroute->oif_ttls[i]) {
					struct interface *oifp = if_lookup_by_index(i);
					vty_out(vty, " ip mroute %s %s %s%s", oifp->name, gbuf, sbuf, VTY_NEWLINE);
					count++;
				}
			}
		}
	}

	return count;
}
