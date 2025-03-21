/* NULL kernel methods for testing. */

/* 
 * Copyright (C) 2006 Sun Microsystems, Inc.
 *
 * This file is part of Quagga.
 *
 * Quagga is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * Quagga is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Quagga; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include <zebra.h>
#include <log.h>

#include "zebra/zserv.h"
#include "zebra/rt.h"
#include "zebra/redistribute.h"
#include "zebra/connected.h"
#include "zebra/rib.h"

int kernel_route_rib(struct prefix *a, struct rib *old, struct rib *new) {
	return 0;
}

int kernel_add_route(struct prefix_ipv4 *a, struct in_addr *b, int c, int d) {
	return 0;
}

int kernel_address_add_ipv4(struct interface *a, struct connected *b) {
	zlog_debug("%s", __func__);
	SET_FLAG(b->conf, ZEBRA_IFC_REAL);
	connected_add_ipv4(a, 0, &b->address->u.prefix4, b->address->prefixlen, (b->destination ? &b->destination->u.prefix4 : NULL), NULL);
	return 0;
}

int kernel_address_delete_ipv4(struct interface *a, struct connected *b) {
	zlog_debug("%s", __func__);
	connected_delete_ipv4(a, 0, &b->address->u.prefix4, b->address->prefixlen, (b->destination ? &b->destination->u.prefix4 : NULL));
	return 0;
}

void kernel_init(struct zebra_vrf *zvrf) {
	return;
}

void kernel_terminate(struct zebra_vrf *zvrf) {
	return;
}
#ifdef HAVE_SYS_WEAK_ALIAS_PRAGMA
	#pragma weak route_read = kernel_init
#else
void route_read(struct zebra_vrf *zvrf) {
	return;
}
#endif
