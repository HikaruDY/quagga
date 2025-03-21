/*
 * kernel routing table update prototype.
 * Copyright (C) 1998 Kunihiro Ishiguro
 * (C)2024-2025 Hikaru Yamatohimemiya
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
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _ZEBRA_RT_H
#define _ZEBRA_RT_H

#include "if.h"
#include "prefix.h"
#include "zebra/rib.h"

extern int kernel_route_rib(struct prefix *, struct rib *, struct rib *);
extern int kernel_add_route(struct prefix_ipv4 *, struct in_addr *, int, int);
extern int kernel_address_add_ipv4(struct interface *, struct connected *);
extern int kernel_address_delete_ipv4(struct interface *, struct connected *);

#endif /* _ZEBRA_RT_H */
