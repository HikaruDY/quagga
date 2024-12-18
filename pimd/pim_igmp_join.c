/*
  PIM for Quagga
  Copyright (C) 2008  Everton da Silva Marques

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

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include "zebra.h"
#include "pim_igmp_join.h"

#ifndef SOL_IP
	#define SOL_IP IPPROTO_IP
#endif

#ifndef MCAST_JOIN_SOURCE_GROUP
	#define MCAST_JOIN_SOURCE_GROUP 46

struct group_source_req {
	uint32_t gsr_interface;
	struct sockaddr_storage gsr_group;
	struct sockaddr_storage gsr_source;
};
#endif

int pim_igmp_join_source(int fd, ifindex_t ifindex, struct in_addr group_addr, struct in_addr source_addr) {
	struct group_source_req req;
	struct sockaddr_in group;
	struct sockaddr_in source;

	memset(&group, 0, sizeof(group));
	group.sin_family = AF_INET;
	group.sin_addr = group_addr;
	group.sin_port = htons(0);
	memcpy(&req.gsr_group, &group, sizeof(struct sockaddr_in));

	memset(&source, 0, sizeof(source));
	source.sin_family = AF_INET;
	source.sin_addr = source_addr;
	source.sin_port = htons(0);
	memcpy(&req.gsr_source, &source, sizeof(struct sockaddr_in));

	req.gsr_interface = ifindex;

	return setsockopt(fd, SOL_IP, MCAST_JOIN_SOURCE_GROUP, &req, sizeof(req));

	return 0;
}
