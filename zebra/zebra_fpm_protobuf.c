/*
 * zebra_fpm_protobuf.c
 *
 * @copyright Copyright (C) 2016 Sproute Networks, Inc.
 *
 * @author Avneesh Sachdev <avneesh@sproute.com>
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

#include "log.h"
#include "rib.h"

#include "qpb/qpb.pb-c.h"
#include "qpb/qpb.h"
#include "qpb/qpb_allocator.h"
#include "qpb/linear_allocator.h"
#include "fpm/fpm_pb.h"

#include "zebra_fpm_private.h"

/*
 * create_delete_route_message
 */
static Fpm__DeleteRoute *create_delete_route_message(qpb_allocator_t *allocator, rib_dest_t *dest, struct rib *rib) {
	Fpm__DeleteRoute *msg;

	msg = QPB_ALLOC(allocator, typeof(*msg));
	if(!msg) {
		assert(0);
		return NULL;
	}

	fpm__delete_route__init(msg);
	msg->vrf_id = rib_dest_vrf(dest)->vrf_id;

	qpb_address_family_set(&msg->address_family, rib_dest_af(dest));

	/*
   * XXX Hardcode subaddress family for now.
   */
	msg->sub_address_family = QPB__SUB_ADDRESS_FAMILY__UNICAST;
	msg->key = fpm_route_key_create(allocator, rib_dest_prefix(dest));
	if(!msg->key) {
		assert(0);
		return NULL;
	}

	return msg;
}

/*
 * add_nexthop
 */
static inline int add_nexthop(qpb_allocator_t *allocator, Fpm__AddRoute *msg, rib_dest_t *dest, struct nexthop *nexthop) {
	uint32_t if_index;
	union g_addr *gateway, *src;

	gateway = src = NULL;

	if_index = nexthop->ifindex;

	if(nexthop->type == NEXTHOP_TYPE_IPV4 || nexthop->type == NEXTHOP_TYPE_IPV4_IFINDEX) {
		gateway = &nexthop->gate;
		if(nexthop->src.ipv4.s_addr) {
			src = &nexthop->src;
		}
	}

	if(nexthop->type == NEXTHOP_TYPE_IPV6 || nexthop->type == NEXTHOP_TYPE_IPV6_IFNAME || nexthop->type == NEXTHOP_TYPE_IPV6_IFINDEX) {
		gateway = &nexthop->gate;
	}

	if(nexthop->type == NEXTHOP_TYPE_IFINDEX || nexthop->type == NEXTHOP_TYPE_IFNAME) {
		if(nexthop->src.ipv4.s_addr) {
			src = &nexthop->src;
		}
	}

	if(!gateway && if_index == 0) {
		return 0;
	}

	/*
   * We have a valid nexthop.
   */
	{
		Fpm__Nexthop *pb_nh;
		pb_nh = QPB_ALLOC(allocator, typeof(*pb_nh));
		if(!pb_nh) {
			assert(0);
			return 0;
		}

		fpm__nexthop__init(pb_nh);

		if(if_index != 0) {
			pb_nh->if_id = qpb_if_identifier_create(allocator, if_index);
		}

		if(gateway) {
			pb_nh->address = qpb_l3_address_create(allocator, gateway, rib_dest_af(dest));
		}

		msg->nexthops[msg->n_nexthops++] = pb_nh;
	}

	// TODO: Use src.

	return 1;
}

/*
 * create_add_route_message
 */
static Fpm__AddRoute *create_add_route_message(qpb_allocator_t *allocator, rib_dest_t *dest, struct rib *rib) {
	Fpm__AddRoute *msg;
	int discard;
	struct nexthop *nexthop, *tnexthop;
	int recursing;
	uint num_nhs, u;
	struct nexthop *nexthops[MAX(MULTIPATH_NUM, 64)];

	msg = QPB_ALLOC(allocator, typeof(*msg));
	if(!msg) {
		assert(0);
		return NULL;
	}

	fpm__add_route__init(msg);

	msg->vrf_id = rib_dest_vrf(dest)->vrf_id;

	qpb_address_family_set(&msg->address_family, rib_dest_af(dest));

	/*
   * XXX Hardcode subaddress family for now.
   */
	msg->sub_address_family = QPB__SUB_ADDRESS_FAMILY__UNICAST;
	msg->key = fpm_route_key_create(allocator, rib_dest_prefix(dest));
	qpb_protocol_set(&msg->protocol, rib->type);

	if((rib->flags & ZEBRA_FLAG_BLACKHOLE) || (rib->flags & ZEBRA_FLAG_REJECT)) {
		discard = 1;
	} else {
		discard = 0;
	}

	if(discard) {
		if(rib->flags & ZEBRA_FLAG_BLACKHOLE) {
			msg->route_type = FPM__ROUTE_TYPE__BLACKHOLE;
		} else if(rib->flags & ZEBRA_FLAG_REJECT) {
			msg->route_type = FPM__ROUTE_TYPE__UNREACHABLE;
		} else {
			assert(0);
		}
		return msg;
	} else {
		msg->route_type = FPM__ROUTE_TYPE__NORMAL;
	}

	msg->metric = rib->metric;

	/*
   * Figure out the set of nexthops to be added to the message.
   */
	num_nhs = 0;
	for(ALL_NEXTHOPS_RO(rib->nexthop, nexthop, tnexthop, recursing)) {
		if(MULTIPATH_NUM != 0 && num_nhs >= MULTIPATH_NUM) {
			break;
		}

		if(num_nhs >= ZEBRA_NUM_OF(nexthops)) {
			break;
		}

		if(CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_RECURSIVE)) {
			continue;
		}

		if(!CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ACTIVE)) {
			continue;
		}

		nexthops[num_nhs] = nexthop;
		num_nhs++;
	}

	if(!num_nhs) {
		zfpm_debug("netlink_encode_route(): No useful nexthop.");
		assert(0);
		return NULL;
	}

	/*
   * And add them to the message.
   */
	if(!(msg->nexthops = qpb_alloc_ptr_array(allocator, num_nhs))) {
		assert(0);
		return NULL;
	}

	msg->n_nexthops = 0;
	for(u = 0; u < num_nhs; u++) {
		if(!add_nexthop(allocator, msg, dest, nexthops[u])) {
			assert(0);
			return NULL;
		}
	}

	assert(msg->n_nexthops == num_nhs);

	return msg;
}

/*
 * create_route_message
 */
static Fpm__Message *create_route_message(qpb_allocator_t *allocator, rib_dest_t *dest, struct rib *rib) {
	Fpm__Message *msg;

	msg = QPB_ALLOC(allocator, typeof(*msg));
	if(!msg) {
		assert(0);
		return NULL;
	}

	fpm__message__init(msg);

	if(!rib) {
		msg->type = FPM__MESSAGE__TYPE__DELETE_ROUTE;
		msg->delete_route = create_delete_route_message(allocator, dest, rib);
		if(!msg->delete_route) {
			assert(0);
			return NULL;
		}
		return msg;
	}

	msg->type = FPM__MESSAGE__TYPE__ADD_ROUTE;
	msg->add_route = create_add_route_message(allocator, dest, rib);
	if(!msg->add_route) {
		assert(0);
		return NULL;
	}

	return msg;
}

/*
 * zfpm_protobuf_encode_route
 *
 * Create a protobuf message corresponding to the given route in the
 * given buffer space.
 *
 * Returns the number of bytes written to the buffer. 0 or a negative
 * value indicates an error.
 */
int zfpm_protobuf_encode_route(rib_dest_t *dest, struct rib *rib, uint8_t *in_buf, size_t in_buf_len) {
	Fpm__Message *msg;
	QPB_DECLARE_STACK_ALLOCATOR(allocator, 4096);
	size_t len;

	QPB_INIT_STACK_ALLOCATOR(allocator);

	msg = create_route_message(&allocator, dest, rib);
	if(!msg) {
		assert(0);
		return 0;
	}

	len = fpm__message__pack(msg, (uint8_t *) in_buf);
	assert(len <= in_buf_len);

	QPB_RESET_STACK_ALLOCATOR(allocator);
	return len;
}
