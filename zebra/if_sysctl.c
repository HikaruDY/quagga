/*
 * Get interface's address and mask information by sysctl() function.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
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

#include <zebra.h>

#include "if.h"
#include "sockunion.h"
#include "prefix.h"
#include "connected.h"
#include "memory.h"
#include "ioctl.h"
#include "log.h"
#include "interface.h"
#include "vrf.h"

#include "zebra/rt.h"
#include "zebra/kernel_socket.h"
#include "zebra/rib.h"

void ifstat_update_sysctl(void) {
	caddr_t ref, buf, end;
	size_t bufsiz;
	struct if_msghdr *ifm;
	struct interface *ifp;

#define MIBSIZ 6
	int mib[MIBSIZ] = { CTL_NET,	   PF_ROUTE, 0, 0, /*  AF_INET & AF_INET6 */
			    NET_RT_IFLIST, 0 };

	/* Query buffer size. */
	if(sysctl(mib, MIBSIZ, NULL, &bufsiz, NULL, 0) < 0) {
		zlog_warn("sysctl() error by %s", safe_strerror(errno));
		return;
	}

	/* We free this memory at the end of this function. */
	ref = buf = XMALLOC(MTYPE_TMP, bufsiz);

	/* Fetch interface informations into allocated buffer. */
	if(sysctl(mib, MIBSIZ, buf, &bufsiz, NULL, 0) < 0) {
		zlog(NULL, LOG_WARNING, "sysctl error by %s", safe_strerror(errno));
		return;
	}

	/* Parse both interfaces and addresses. */
	for(end = buf + bufsiz; buf < end; buf += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *) buf;
		if(ifm->ifm_type == RTM_IFINFO) {
			ifp = if_lookup_by_index(ifm->ifm_index);
			if(ifp) {
				ifp->stats = ifm->ifm_data;
			}
		}
	}

	/* Free sysctl buffer. */
	XFREE(MTYPE_TMP, ref);

	return;
}

/* Interface listing up function using sysctl(). */
void interface_list(struct zebra_vrf *zvrf) {
	caddr_t ref, buf, end;
	size_t bufsiz;
	struct if_msghdr *ifm;

#define MIBSIZ 6
	int mib[MIBSIZ] = { CTL_NET,	   PF_ROUTE, 0, 0, /*  AF_INET & AF_INET6 */
			    NET_RT_IFLIST, 0 };

	if(zvrf->vrf_id != VRF_DEFAULT) {
		zlog_warn("interface_list: ignore VRF %u", zvrf->vrf_id);
		return;
	}

	/* Query buffer size. */
	if(sysctl(mib, MIBSIZ, NULL, &bufsiz, NULL, 0) < 0) {
		zlog(NULL, LOG_WARNING, "sysctl() error by %s", safe_strerror(errno));
		return;
	}

	/* We free this memory at the end of this function. */
	ref = buf = XMALLOC(MTYPE_TMP, bufsiz);

	/* Fetch interface informations into allocated buffer. */
	if(sysctl(mib, MIBSIZ, buf, &bufsiz, NULL, 0) < 0) {
		zlog(NULL, LOG_WARNING, "sysctl error by %s", safe_strerror(errno));
		return;
	}

	/* Parse both interfaces and addresses. */
	for(end = buf + bufsiz; buf < end; buf += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *) buf;

		switch(ifm->ifm_type) {
			case RTM_IFINFO: ifm_read(ifm); break;
			case RTM_NEWADDR: ifam_read((struct ifa_msghdr *) ifm); break;
			default:
				zlog_info("interfaces_list(): unexpected message type");
				XFREE(MTYPE_TMP, ref);
				return;
				break;
		}
	}

	/* Free sysctl buffer. */
	XFREE(MTYPE_TMP, ref);
}
