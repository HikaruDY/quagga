/* Zebra's client library.
 * Copyright (C) 1999 Kunihiro Ishiguro
 * (C)2024-2025 Hikaru Yamatohimemiya
 * Copyright (C) 2005 Andrew J. Schorr
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include <zebra.h>
#include <sigevent.h>

#include "prefix.h"
#include "stream.h"
#include "buffer.h"
#include "network.h"
#include "if.h"
#include "log.h"
#include "thread.h"
#include "zclient.h"
#include "memory.h"
#include "table.h"

/* Zebra client events. */
enum event {
	ZCLIENT_SCHEDULE,
	ZCLIENT_READ,
	ZCLIENT_CONNECT
};

/* Prototype for event manager. */
static void zclient_event(enum event, struct zclient *);

const char *zclient_serv_path = NULL;

/* This file local debug flag. */
int zclient_debug = 0;

/* Allocate zclient structure. */
struct zclient *zclient_new(struct thread_master *master) {
	struct zclient *zclient;
	zclient = XCALLOC(MTYPE_ZCLIENT, sizeof(struct zclient));

	zclient->ibuf = stream_new(ZEBRA_MAX_PACKET_SIZ);
	zclient->obuf = stream_new(ZEBRA_MAX_PACKET_SIZ);
	zclient->wb = buffer_new(0);
	zclient->master = master;

	return zclient;
}

/* This function is only called when exiting, because
   many parts of the code do not check for I/O errors, so they could
   reference an invalid pointer if the structure was ever freed.

   Free zclient structure. */
void zclient_free(struct zclient *zclient) {
	if(zclient->ibuf) {
		stream_free(zclient->ibuf);
	}
	if(zclient->obuf) {
		stream_free(zclient->obuf);
	}
	if(zclient->wb) {
		buffer_free(zclient->wb);
	}

	XFREE(MTYPE_ZCLIENT, zclient);
}

/* Initialize zebra client.  Argument redist_default is unwanted
   redistribute route type. */
void zclient_init(struct zclient *zclient, int redist_default) {
	int i;

	/* Enable zebra client connection by default. */
	zclient->enable = 1;

	/* Set -1 to the default socket value. */
	zclient->sock = -1;

	/* Clear redistribution flags. */
	for(i = 0; i < ZEBRA_ROUTE_MAX; i++) {
		zclient->redist[i] = vrf_bitmap_init();
	}

	/* Set unwanted redistribute route.  bgpd does not need BGP route
     redistribution. */
	zclient->redist_default = redist_default;

	/* Set default-information redistribute to zero. */
	zclient->default_information = vrf_bitmap_init();

	/* Schedule first zclient connection. */
	if(zclient_debug) {
		zlog_debug("zclient start scheduled");
	}

	zclient_event(ZCLIENT_SCHEDULE, zclient);
}

/* Stop zebra client services. */
void zclient_stop(struct zclient *zclient) {
	int i;

	if(zclient_debug) {
		zlog_debug("zclient stopped");
	}

	/* Stop threads. */
	THREAD_OFF(zclient->t_read);
	THREAD_OFF(zclient->t_connect);
	THREAD_OFF(zclient->t_write);

	/* Reset streams. */
	stream_reset(zclient->ibuf);
	stream_reset(zclient->obuf);

	/* Empty the write buffer. */
	buffer_reset(zclient->wb);

	/* Close socket. */
	if(zclient->sock >= 0) {
		close(zclient->sock);
		zclient->sock = -1;
	}
	zclient->fail = 0;

	for(i = 0; i < ZEBRA_ROUTE_MAX; i++) {
		vrf_bitmap_free(zclient->redist[i]);
		zclient->redist[i] = VRF_BITMAP_NULL;
	}
	vrf_bitmap_free(zclient->default_information);
	zclient->default_information = VRF_BITMAP_NULL;
}

void zclient_reset(struct zclient *zclient) {
	zclient_stop(zclient);
	zclient_init(zclient, zclient->redist_default);
}

#ifdef HAVE_TCP_ZEBRA

/* Make socket to zebra daemon. Return zebra socket. */
static int zclient_socket(void) {
	int sock;
	int ret;
	struct sockaddr_in serv;

	/* We should think about IPv6 connection. */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0) {
		return -1;
	}

	/* Make server socket. */
	memset(&serv, 0, sizeof(struct sockaddr_in));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(ZEBRA_PORT);
	#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
	serv.sin_len = sizeof(struct sockaddr_in);
	#endif /* HAVE_STRUCT_SOCKADDR_IN_SIN_LEN */
	serv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/* Connect to zebra. */
	ret = connect(sock, (struct sockaddr *) &serv, sizeof(serv));
	if(ret < 0) {
		zlog_warn("%s connect failure: %d", __PRETTY_FUNCTION__, errno);
		close(sock);
		return -1;
	}
	return sock;
}

#else

	/* For sockaddr_un. */
	#include <sys/un.h>

static int zclient_socket_un(const char *path) {
	int ret;
	int sock, len;
	struct sockaddr_un addr;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock < 0) {
		return -1;
	}

	/* Make server socket. */
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, strlen(path));
	#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	len = addr.sun_len = SUN_LEN(&addr);
	#else
	len = sizeof(addr.sun_family) + strlen(addr.sun_path);
	#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */

	ret = connect(sock, (struct sockaddr *) &addr, len);
	if(ret < 0) {
		zlog_warn("%s connect failure: %d", __PRETTY_FUNCTION__, errno);
		close(sock);
		return -1;
	}
	return sock;
}

#endif /* HAVE_TCP_ZEBRA */

/**
 * Connect to zebra daemon.
 * @param zclient a pointer to zclient structure
 * @return socket fd just to make sure that connection established
 * @see zclient_init
 * @see zclient_new
 */
int zclient_socket_connect(struct zclient *zclient) {
#ifdef HAVE_TCP_ZEBRA
	zclient->sock = zclient_socket();
#else
	zclient->sock = zclient_socket_un(zclient_serv_path_get());
#endif
	return zclient->sock;
}

static int zclient_failed(struct zclient *zclient) {
	zclient->fail++;
	zclient_stop(zclient);
	zclient_event(ZCLIENT_CONNECT, zclient);
	return -1;
}

static int zclient_flush_data(struct thread *thread) {
	struct zclient *zclient = THREAD_ARG(thread);

	zclient->t_write = NULL;
	if(zclient->sock < 0) {
		return -1;
	}
	switch(buffer_flush_available(zclient->wb, zclient->sock)) {
		case BUFFER_ERROR:
			zlog_warn("%s: buffer_flush_available failed on zclient fd %d, closing", __func__, zclient->sock);
			return zclient_failed(zclient);
			break;
		case BUFFER_PENDING: zclient->t_write = thread_add_write(zclient->master, zclient_flush_data, zclient, zclient->sock); break;
		case BUFFER_EMPTY: break;
	}
	return 0;
}

int zclient_send_message(struct zclient *zclient) {
	if(zclient->sock < 0) {
		return -1;
	}
	switch(buffer_write(zclient->wb, zclient->sock, STREAM_DATA(zclient->obuf), stream_get_endp(zclient->obuf))) {
		case BUFFER_ERROR:
			zlog_warn("%s: buffer_write failed to zclient fd %d, closing", __func__, zclient->sock);
			return zclient_failed(zclient);
			break;
		case BUFFER_EMPTY: THREAD_OFF(zclient->t_write); break;
		case BUFFER_PENDING: THREAD_WRITE_ON(zclient->master, zclient->t_write, zclient_flush_data, zclient, zclient->sock); break;
	}
	return 0;
}

void zclient_create_header(struct stream *s, uint16_t command, vrf_id_t vrf_id) {
	/* length placeholder, caller can update */
	stream_putw(s, ZEBRA_HEADER_SIZE);
	stream_putc(s, ZEBRA_HEADER_MARKER);
	stream_putc(s, ZSERV_VERSION);
	stream_putw(s, vrf_id);
	stream_putw(s, command);
}

int zclient_read_header(struct stream *s, int sock, u_int16_t *size, u_char *marker, u_char *version, u_int16_t *vrf_id, u_int16_t *cmd) {
	if(stream_read(s, sock, ZEBRA_HEADER_SIZE) != ZEBRA_HEADER_SIZE) {
		return -1;
	}

	*size = stream_getw(s) - ZEBRA_HEADER_SIZE;
	*marker = stream_getc(s);
	*version = stream_getc(s);
	*vrf_id = stream_getw(s);
	*cmd = stream_getw(s);

	if(*version != ZSERV_VERSION || *marker != ZEBRA_HEADER_MARKER) {
		zlog_err("%s: socket %d version mismatch, marker %d, version %d", __func__, sock, *marker, *version);
		return -1;
	}

	if(*size && stream_read(s, sock, *size) != *size) {
		return -1;
	}

	return 0;
}

/* Send simple Zebra message. */
static int zebra_message_send(struct zclient *zclient, int command, vrf_id_t vrf_id) {
	struct stream *s;

	/* Get zclient output buffer. */
	s = zclient->obuf;
	stream_reset(s);

	/* Send very simple command only Zebra message. */
	zclient_create_header(s, command, vrf_id);

	return zclient_send_message(zclient);
}

static int zebra_hello_send(struct zclient *zclient) {
	struct stream *s;

	if(zclient->redist_default) {
		s = zclient->obuf;
		stream_reset(s);

		/* The VRF ID in the HELLO message is always 0. */
		zclient_create_header(s, ZEBRA_HELLO, VRF_DEFAULT);
		stream_putc(s, zclient->redist_default);
		stream_putw_at(s, 0, stream_get_endp(s));
		return zclient_send_message(zclient);
	}

	return 0;
}

/* Send requests to zebra daemon for the information in a VRF. */
void zclient_send_requests(struct zclient *zclient, vrf_id_t vrf_id) {
	int i;

	/* zclient is disabled. */
	if(!zclient->enable) {
		return;
	}

	/* If not connected to the zebra yet. */
	if(zclient->sock < 0) {
		return;
	}

	if(zclient_debug) {
		zlog_debug("%s: send messages for VRF %u", __func__, vrf_id);
	}

	/* We need router-id information. */
	zebra_message_send(zclient, ZEBRA_ROUTER_ID_ADD, vrf_id);

	/* We need interface information. */
	zebra_message_send(zclient, ZEBRA_INTERFACE_ADD, vrf_id);

	/* Set unwanted redistribute route. */
	vrf_bitmap_set(zclient->redist[zclient->redist_default], vrf_id);

	/* Flush all redistribute request. */
	for(i = 0; i < ZEBRA_ROUTE_MAX; i++) {
		if(i != zclient->redist_default && vrf_bitmap_check(zclient->redist[i], vrf_id)) {
			zebra_redistribute_send(ZEBRA_REDISTRIBUTE_ADD, zclient, i, vrf_id);
		}
	}

	/* If default information is needed. */
	if(vrf_bitmap_check(zclient->default_information, VRF_DEFAULT)) {
		zebra_message_send(zclient, ZEBRA_REDISTRIBUTE_DEFAULT_ADD, vrf_id);
	}
}

/* Make connection to zebra daemon. */
int zclient_start(struct zclient *zclient) {
	if(zclient_debug) {
		zlog_debug("zclient_start is called");
	}

	/* zclient is disabled. */
	if(!zclient->enable) {
		return 0;
	}

	/* If already connected to the zebra. */
	if(zclient->sock >= 0) {
		return 0;
	}

	/* Check connect thread. */
	if(zclient->t_connect) {
		return 0;
	}

	/*
   * If we fail to connect to the socket on initialization,
   * Let's wait a second and see if we can reconnect.
   * Cause if we don't connect, we never attempt to
   * reconnect.  On startup if zebra is slow we
   * can get into this situation.
   */
	while(zclient_socket_connect(zclient) < 0) {
		zlog_warn("W: Waiting zebra...");
		quagga_sigevent_process();
		sleep(1);
	}

	if(zclient->sock < 0) {
		zclient_event(ZCLIENT_CONNECT, zclient);
		return -1;
	}

	if(set_nonblocking(zclient->sock) < 0) {
		zlog_warn("%s: set_nonblocking(%d) failed", __func__, zclient->sock);
	}

	/* Clear fail count. */
	zclient->fail = 0;
	if(zclient_debug) {
		zlog_debug("zclient connect success with socket [%d]", zclient->sock);
	}

	/* Create read thread. */
	zclient_event(ZCLIENT_READ, zclient);

	zebra_hello_send(zclient);

	/* Inform the successful connection. */
	if(zclient->zebra_connected) {
		(*zclient->zebra_connected)(zclient);
	}

	return 0;
}

/* This function is a wrapper function for calling zclient_start from
   timer or event thread. */
static int zclient_connect(struct thread *t) {
	struct zclient *zclient;

	zclient = THREAD_ARG(t);
	zclient->t_connect = NULL;

	if(zclient_debug) {
		zlog_debug("zclient_connect is called");
	}

	return zclient_start(zclient);
}

/*
  * "xdr_encode"-like interface that allows daemon (client) to send
  * a message to zebra server for a route that needs to be
  * added/deleted to the kernel. Info about the route is specified
  * by the caller in a struct zapi_ipv4. zapi_ipv4_read() then writes
  * the info down the zclient socket using the stream_* functions.
  *
  * The corresponding read ("xdr_decode") function on the server
  * side is zread_ipv4_add()/zread_ipv4_delete().
  *
  *  0 1 2 3 4 5 6 7 8 9 A B C D E F 0 1 2 3 4 5 6 7 8 9 A B C D E F
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * |            Length (2)         |    Command    | Route Type    |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * | ZEBRA Flags   | Message Flags | Prefix length |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * | Destination IPv4 Prefix for route                             |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * | Nexthop count |
  * +-+-+-+-+-+-+-+-+
  *
  *
  * A number of IPv4 nexthop(s) or nexthop interface index(es) are then
  * described, as per the Nexthop count. Each nexthop described as:
  *
  * +-+-+-+-+-+-+-+-+
  * | Nexthop Type  |  Set to one of ZEBRA_NEXTHOP_*
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  * |       IPv4 Nexthop address or Interface Index number          |
  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  *
  * Alternatively, if the flags field has ZEBRA_FLAG_BLACKHOLE or
  * ZEBRA_FLAG_REJECT is set then Nexthop count is set to 1, then _no_
  * nexthop information is provided, and the message describes a prefix
  * to blackhole or reject route.
  *
  * If ZAPI_MESSAGE_DISTANCE is set, the distance value is written as a 1
  * byte value.
  *
  * If ZAPI_MESSAGE_METRIC is set, the metric value is written as an 8
  * byte value.
  *
  * If ZAPI_MESSAGE_TAG is set, the tag value is written as a 4 byte value
  *
  * XXX: No attention paid to alignment.
  */
int zapi_ipv4_route(u_char cmd, struct zclient *zclient, struct prefix_ipv4 *p, struct zapi_ipv4 *api) {
	int i;
	int psize;
	struct stream *s;

	/* Reset stream. */
	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, cmd, api->vrf_id);

	/* Put type and nexthop. */
	stream_putc(s, api->type);
	stream_putc(s, api->flags);
	stream_putc(s, api->message);
	stream_putw(s, api->safi);

	/* Put prefix information. */
	psize = PSIZE(p->prefixlen);
	stream_putc(s, p->prefixlen);
	stream_write(s, (u_char *) &p->prefix, psize);

	/* Nexthop, ifindex, distance and metric information. */
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_NEXTHOP)) {
		if(CHECK_FLAG(api->flags, ZEBRA_FLAG_BLACKHOLE)) {
			stream_putc(s, 1);
			stream_putc(s, ZEBRA_NEXTHOP_BLACKHOLE);
			/* XXX assert(api->nexthop_num == 0); */
			/* XXX assert(api->ifindex_num == 0); */
		} else {
			stream_putc(s, api->nexthop_num + api->ifindex_num);
		}

		for(i = 0; i < api->nexthop_num; i++) {
			stream_putc(s, ZEBRA_NEXTHOP_IPV4);
			stream_put_in_addr(s, api->nexthop[i]);
		}
		for(i = 0; i < api->ifindex_num; i++) {
			stream_putc(s, ZEBRA_NEXTHOP_IFINDEX);
			stream_putl(s, api->ifindex[i]);
		}
	}

	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_DISTANCE)) {
		stream_putc(s, api->distance);
	}
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_METRIC)) {
		stream_putl(s, api->metric);
	}
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_MTU)) {
		stream_putl(s, api->mtu);
	}
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_TAG)) {
		stream_putl(s, api->tag);
	}

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

#ifdef HAVE_IPV6
int zapi_ipv6_route(u_char cmd, struct zclient *zclient, struct prefix_ipv6 *p, struct zapi_ipv6 *api) {
	int i;
	int psize;
	struct stream *s;

	/* Reset stream. */
	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, cmd, api->vrf_id);

	/* Put type and nexthop. */
	stream_putc(s, api->type);
	stream_putc(s, api->flags);
	stream_putc(s, api->message);
	stream_putw(s, api->safi);

	/* Put prefix information. */
	psize = PSIZE(p->prefixlen);
	stream_putc(s, p->prefixlen);
	stream_write(s, (u_char *) &p->prefix, psize);

	/* Nexthop, ifindex, distance and metric information. */
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_NEXTHOP)) {
		stream_putc(s, api->nexthop_num + api->ifindex_num);

		for(i = 0; i < api->nexthop_num; i++) {
			stream_putc(s, ZEBRA_NEXTHOP_IPV6);
			stream_write(s, (u_char *) api->nexthop[i], 16);
		}
		for(i = 0; i < api->ifindex_num; i++) {
			stream_putc(s, ZEBRA_NEXTHOP_IFINDEX);
			stream_putl(s, api->ifindex[i]);
		}
	}

	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_DISTANCE)) {
		stream_putc(s, api->distance);
	}
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_METRIC)) {
		stream_putl(s, api->metric);
	}
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_MTU)) {
		stream_putl(s, api->mtu);
	}
	if(CHECK_FLAG(api->message, ZAPI_MESSAGE_TAG)) {
		stream_putl(s, api->tag);
	}

	/* Put length at the first point of the stream. */
	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}
#endif /* HAVE_IPV6 */

/*
 * send a ZEBRA_REDISTRIBUTE_ADD or ZEBRA_REDISTRIBUTE_DELETE
 * for the route type (ZEBRA_ROUTE_KERNEL etc.). The zebra server will
 * then set/unset redist[type] in the client handle (a struct zserv) for the
 * sending client
 */
int zebra_redistribute_send(int command, struct zclient *zclient, int type, vrf_id_t vrf_id) {
	struct stream *s;

	s = zclient->obuf;
	stream_reset(s);

	zclient_create_header(s, command, vrf_id);
	stream_putc(s, type);

	stream_putw_at(s, 0, stream_get_endp(s));

	return zclient_send_message(zclient);
}

/* Get prefix in ZServ format; family should be filled in on prefix */
static void zclient_stream_get_prefix(struct stream *s, struct prefix *p) {
	size_t plen = prefix_blen(p);
	u_char c;
	p->prefixlen = 0;

	if(plen == 0) {
		return;
	}

	stream_get(&p->u.prefix, s, plen);
	c = stream_getc(s);
	p->prefixlen = MIN(plen * 8, c);
}

/* Router-id update from zebra daemon. */
void zebra_router_id_update_read(struct stream *s, struct prefix *rid) {
	/* Fetch interface address. */
	rid->family = stream_getc(s);

	zclient_stream_get_prefix(s, rid);
}

/* Interface addition from zebra daemon. */
/*
 * The format of the message sent with type ZEBRA_INTERFACE_ADD or
 * ZEBRA_INTERFACE_DELETE from zebra to the client is:
 *     0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifname                                                       |
 * |                                                               |
 * |                                                               |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifindex                                                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  status       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  if_flags                                                     |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  metric                                                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifmtu                                                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  ifmtu6                                                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  bandwidth                                                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Link Layer Type                                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Harware Address Length                                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Hardware Address      if HW lenght different from 0          |
 * |   ...                  max INTERFACE_HWADDR_MAX               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Link_params? |  Whether a link-params follows: 1 or 0.
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Link_params    0 or 1 INTERFACE_LINK_PARAMS_SIZE sized       |
 * |   ....          (struct if_link_params).                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct interface *zebra_interface_add_read(struct stream *s, vrf_id_t vrf_id) {
	struct interface *ifp;
	char ifname_tmp[INTERFACE_NAMSIZ];

	/* Read interface name. */
	stream_get(ifname_tmp, s, INTERFACE_NAMSIZ);

	/* Lookup/create interface by name. */
	ifp = if_get_by_name_len_vrf(ifname_tmp, strnlen(ifname_tmp, INTERFACE_NAMSIZ), vrf_id);

	zebra_interface_if_set_value(s, ifp);

	return ifp;
}

/*
 * Read interface up/down msg (ZEBRA_INTERFACE_UP/ZEBRA_INTERFACE_DOWN)
 * from zebra server.  The format of this message is the same as
 * that sent for ZEBRA_INTERFACE_ADD/ZEBRA_INTERFACE_DELETE (see
 * comments for zebra_interface_add_read), except that no sockaddr_dl
 * is sent at the tail of the message.
 */
struct interface *zebra_interface_state_read(struct stream *s, vrf_id_t vrf_id) {
	struct interface *ifp;
	char ifname_tmp[INTERFACE_NAMSIZ];

	/* Read interface name. */
	stream_get(ifname_tmp, s, INTERFACE_NAMSIZ);

	/* Lookup this by interface index. */
	ifp = if_lookup_by_name_len_vrf(ifname_tmp, strnlen(ifname_tmp, INTERFACE_NAMSIZ), vrf_id);

	/* If such interface does not exist, indicate an error */
	if(!ifp) {
		return NULL;
	}

	zebra_interface_if_set_value(s, ifp);

	return ifp;
}

static void link_params_set_value(struct stream *s, struct if_link_params *iflp) {
	if(iflp == NULL) {
		return;
	}

	iflp->lp_status = stream_getl(s);
	iflp->te_metric = stream_getl(s);
	iflp->max_bw = stream_getf(s);
	iflp->max_rsv_bw = stream_getf(s);
	uint32_t bwclassnum = stream_getl(s);
	{
		unsigned int i;
		for(i = 0; i < bwclassnum && i < MAX_CLASS_TYPE; i++) {
			iflp->unrsv_bw[i] = stream_getf(s);
		}
		if(i < bwclassnum) {
			zlog_err(
				"%s: received %d > %d (MAX_CLASS_TYPE) bw entries"
				" - outdated library?",
				__func__, bwclassnum, MAX_CLASS_TYPE
			);
		}
	}
	iflp->admin_grp = stream_getl(s);
	iflp->rmt_as = stream_getl(s);
	iflp->rmt_ip.s_addr = stream_get_ipv4(s);

	iflp->av_delay = stream_getl(s);
	iflp->min_delay = stream_getl(s);
	iflp->max_delay = stream_getl(s);
	iflp->delay_var = stream_getl(s);

	iflp->pkt_loss = stream_getf(s);
	iflp->res_bw = stream_getf(s);
	iflp->ava_bw = stream_getf(s);
	iflp->use_bw = stream_getf(s);
}

struct interface *zebra_interface_link_params_read(struct stream *s) {
	struct if_link_params *iflp;
	uint32_t ifindex = stream_getl(s);

	struct interface *ifp = if_lookup_by_index(ifindex);

	if(ifp == NULL || s == NULL) {
		zlog_err("%s: unknown ifindex %u, shouldn't happen", __func__, ifindex);
		return NULL;
	}

	if((iflp = if_link_params_get(ifp)) == NULL) {
		return NULL;
	}

	link_params_set_value(s, iflp);

	return ifp;
}

void zebra_interface_if_set_value(struct stream *s, struct interface *ifp) {
	u_char link_params_status = 0;

	/* Read interface's index. */
	ifp->ifindex = stream_getl(s);
	ifp->status = stream_getc(s);

	/* Read interface's value. */
	ifp->flags = stream_getq(s);
	ifp->metric = stream_getl(s);
	ifp->mtu = stream_getl(s);
	ifp->mtu6 = stream_getl(s);
	ifp->bandwidth = stream_getl(s);
	ifp->ll_type = stream_getl(s);
	ifp->hw_addr_len = stream_getl(s);
	if(ifp->hw_addr_len) {
		stream_get(ifp->hw_addr, s, MIN(ifp->hw_addr_len, INTERFACE_HWADDR_MAX));
	}

	/* Read Traffic Engineering status */
	link_params_status = stream_getc(s);
	/* Then, Traffic Engineering parameters if any */
	if(link_params_status) {
		struct if_link_params *iflp = if_link_params_get(ifp);
		link_params_set_value(s, iflp);
	}
}

size_t zebra_interface_link_params_write(struct stream *s, struct interface *ifp) {
	size_t w;
	struct if_link_params *iflp;
	int i;

	if(s == NULL || ifp == NULL || ifp->link_params == NULL) {
		return 0;
	}

	iflp = ifp->link_params;
	w = 0;

	w += stream_putl(s, iflp->lp_status);

	w += stream_putl(s, iflp->te_metric);
	w += stream_putf(s, iflp->max_bw);
	w += stream_putf(s, iflp->max_rsv_bw);

	w += stream_putl(s, MAX_CLASS_TYPE);
	for(i = 0; i < MAX_CLASS_TYPE; i++) {
		w += stream_putf(s, iflp->unrsv_bw[i]);
	}

	w += stream_putl(s, iflp->admin_grp);
	w += stream_putl(s, iflp->rmt_as);
	w += stream_put_in_addr(s, &iflp->rmt_ip);

	w += stream_putl(s, iflp->av_delay);
	w += stream_putl(s, iflp->min_delay);
	w += stream_putl(s, iflp->max_delay);
	w += stream_putl(s, iflp->delay_var);

	w += stream_putf(s, iflp->pkt_loss);
	w += stream_putf(s, iflp->res_bw);
	w += stream_putf(s, iflp->ava_bw);
	w += stream_putf(s, iflp->use_bw);

	return w;
}

/*
 * format of message for address additon is:
 *    0
 *  0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+
 * |   type        |  ZEBRA_INTERFACE_ADDRESS_ADD or
 * +-+-+-+-+-+-+-+-+  ZEBRA_INTERFACE_ADDRES_DELETE
 * |               |
 * +               +
 * |   ifindex     |
 * +               +
 * |               |
 * +               +
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |   ifc_flags   |  flags for connected address
 * +-+-+-+-+-+-+-+-+
 * |  addr_family  |
 * +-+-+-+-+-+-+-+-+
 * |    addr...    |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 * |    addr_len   |  len of addr. E.g., addr_len = 4 for ipv4 addrs.
 * +-+-+-+-+-+-+-+-+
 * |     daddr..   |
 * :               :
 * |               |
 * +-+-+-+-+-+-+-+-+
 */

static int memconstant(const void *s, int c, size_t n) {
	const u_char *p = s;

	while(n-- > 0) {
		if(*p++ != c) {
			return 0;
		}
	}
	return 1;
}

struct connected *zebra_interface_address_read(int type, struct stream *s, vrf_id_t vrf_id) {
	ifindex_t ifindex;
	struct interface *ifp;
	struct connected *ifc;
	struct prefix p, d, *dp;
	int plen;
	u_char ifc_flags;

	memset(&p, 0, sizeof(p));
	memset(&d, 0, sizeof(d));

	/* Get interface index. */
	ifindex = stream_getl(s);

	/* Lookup index. */
	ifp = if_lookup_by_index_vrf(ifindex, vrf_id);
	if(ifp == NULL) {
		zlog_warn(
			"zebra_interface_address_read(%s): "
			"Can't find interface by ifindex: %d ",
			(type == ZEBRA_INTERFACE_ADDRESS_ADD ? "ADD" : "DELETE"), ifindex
		);
		return NULL;
	}

	/* Fetch flag. */
	ifc_flags = stream_getc(s);

	/* Fetch interface address. */
	d.family = p.family = stream_getc(s);
	plen = prefix_blen(&d);

	zclient_stream_get_prefix(s, &p);

	/* Fetch destination address. */
	stream_get(&d.u.prefix, s, plen);

	/* N.B. NULL destination pointers are encoded as all zeroes */
	dp = memconstant(&d.u.prefix, 0, plen) ? NULL : &d;

	if(type == ZEBRA_INTERFACE_ADDRESS_ADD) {
		/* N.B. NULL destination pointers are encoded as all zeroes */
		ifc = connected_add_by_prefix(ifp, &p, dp);
		if(ifc != NULL) {
			ifc->flags = ifc_flags;
			if(ifc->destination) {
				ifc->destination->prefixlen = ifc->address->prefixlen;
			} else if(CHECK_FLAG(ifc->flags, ZEBRA_IFA_PEER)) {
				/* carp interfaces on OpenBSD with 0.0.0.0/0 as "peer" */
				char buf[PREFIX_STRLEN];
				zlog_warn(
					"warning: interface %s address %s "
					"with peer flag set, but no peer address!",
					ifp->name, prefix2str(ifc->address, buf, sizeof buf)
				);
				UNSET_FLAG(ifc->flags, ZEBRA_IFA_PEER);
			}
		}
	} else {
		assert(type == ZEBRA_INTERFACE_ADDRESS_DELETE);
		ifc = connected_delete_by_prefix(ifp, &p);
	}

	return ifc;
}

/* Zebra client message read function. */
static int zclient_read(struct thread *thread) {
	size_t already;
	uint16_t length, command;
	uint8_t marker, version;
	vrf_id_t vrf_id;
	struct zclient *zclient;

	/* Get socket to zebra. */
	zclient = THREAD_ARG(thread);
	zclient->t_read = NULL;

	/* Read zebra header (if we don't have it already). */
	if((already = stream_get_endp(zclient->ibuf)) < ZEBRA_HEADER_SIZE) {
		ssize_t nbyte;
		if(((nbyte = stream_read_try(zclient->ibuf, zclient->sock, ZEBRA_HEADER_SIZE - already)) == 0) || (nbyte == -1)) {
			if(zclient_debug) {
				zlog_debug("zclient connection closed socket [%d].", zclient->sock);
			}
			return zclient_failed(zclient);
		}
		if(nbyte != (ssize_t) (ZEBRA_HEADER_SIZE - already)) {
			/* Try again later. */
			zclient_event(ZCLIENT_READ, zclient);
			return 0;
		}
		already = ZEBRA_HEADER_SIZE;
	}

	/* Reset to read from the beginning of the incoming packet. */
	stream_set_getp(zclient->ibuf, 0);

	/* Fetch header values. */
	length = stream_getw(zclient->ibuf);
	marker = stream_getc(zclient->ibuf);
	version = stream_getc(zclient->ibuf);
	vrf_id = stream_getw(zclient->ibuf);
	command = stream_getw(zclient->ibuf);

	if(marker != ZEBRA_HEADER_MARKER || version != ZSERV_VERSION) {
		zlog_err("%s: socket %d version mismatch, marker %d, version %d", __func__, zclient->sock, marker, version);
		return zclient_failed(zclient);
	}

	if(length < ZEBRA_HEADER_SIZE) {
		zlog_err("%s: socket %d message length %u is less than %d ", __func__, zclient->sock, length, ZEBRA_HEADER_SIZE);
		return zclient_failed(zclient);
	}

	/* Length check. */
	if(length > STREAM_SIZE(zclient->ibuf)) {
		struct stream *ns;
		zlog_warn("%s: message size %u exceeds buffer size %lu, expanding...", __func__, length, (u_long) STREAM_SIZE(zclient->ibuf));
		ns = stream_new(length);
		stream_copy(ns, zclient->ibuf);
		stream_free(zclient->ibuf);
		zclient->ibuf = ns;
	}

	/* Read rest of zebra packet. */
	if(already < length) {
		ssize_t nbyte;
		if(((nbyte = stream_read_try(zclient->ibuf, zclient->sock, length - already)) == 0) || (nbyte == -1)) {
			if(zclient_debug) {
				zlog_debug("zclient connection closed socket [%d].", zclient->sock);
			}
			return zclient_failed(zclient);
		}
		if(nbyte != (ssize_t) (length - already)) {
			/* Try again later. */
			zclient_event(ZCLIENT_READ, zclient);
			return 0;
		}
	}

	length -= ZEBRA_HEADER_SIZE;

	if(zclient_debug) {
		zlog_debug("zclient 0x%p command 0x%x VRF %u\n", (void *) zclient, command, vrf_id);
	}

	switch(command) {
		case ZEBRA_ROUTER_ID_UPDATE:
			if(zclient->router_id_update) {
				(*zclient->router_id_update)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_ADD:
			if(zclient->interface_add) {
				(*zclient->interface_add)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_DELETE:
			if(zclient->interface_delete) {
				(*zclient->interface_delete)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_ADDRESS_ADD:
			if(zclient->interface_address_add) {
				(*zclient->interface_address_add)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_ADDRESS_DELETE:
			if(zclient->interface_address_delete) {
				(*zclient->interface_address_delete)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_UP:
			if(zclient->interface_up) {
				(*zclient->interface_up)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_DOWN:
			if(zclient->interface_down) {
				(*zclient->interface_down)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_IPV4_ROUTE_ADD:
			if(zclient->ipv4_route_add) {
				(*zclient->ipv4_route_add)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_IPV4_ROUTE_DELETE:
			if(zclient->ipv4_route_delete) {
				(*zclient->ipv4_route_delete)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_IPV6_ROUTE_ADD:
			if(zclient->ipv6_route_add) {
				(*zclient->ipv6_route_add)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_IPV6_ROUTE_DELETE:
			if(zclient->ipv6_route_delete) {
				(*zclient->ipv6_route_delete)(command, zclient, length, vrf_id);
			}
			break;
		case ZEBRA_INTERFACE_LINK_PARAMS:
			if(zclient->interface_link_params) {
				(*zclient->interface_link_params)(command, zclient, length);
			}
		case ZEBRA_NEXTHOP_UPDATE:
			if(zclient->nexthop_update) {
				(*zclient->nexthop_update)(command, zclient, length, vrf_id);
			}
			break;
		default: break;
	}

	if(zclient->sock < 0) {
		/* Connection was closed during packet processing. */
		return -1;
	}

	/* Register read thread. */
	stream_reset(zclient->ibuf);
	zclient_event(ZCLIENT_READ, zclient);

	return 0;
}

void zclient_redistribute(int command, struct zclient *zclient, int type, vrf_id_t vrf_id) {
	if(command == ZEBRA_REDISTRIBUTE_ADD) {
		if(vrf_bitmap_check(zclient->redist[type], vrf_id)) {
			return;
		}
		vrf_bitmap_set(zclient->redist[type], vrf_id);
	} else {
		if(!vrf_bitmap_check(zclient->redist[type], vrf_id)) {
			return;
		}
		vrf_bitmap_unset(zclient->redist[type], vrf_id);
	}

	if(zclient->sock > 0) {
		zebra_redistribute_send(command, zclient, type, vrf_id);
	}
}

void zclient_redistribute_default(int command, struct zclient *zclient, vrf_id_t vrf_id) {
	if(command == ZEBRA_REDISTRIBUTE_DEFAULT_ADD) {
		if(vrf_bitmap_check(zclient->default_information, vrf_id)) {
			return;
		}
		vrf_bitmap_set(zclient->default_information, vrf_id);
	} else {
		if(!vrf_bitmap_check(zclient->default_information, vrf_id)) {
			return;
		}
		vrf_bitmap_unset(zclient->default_information, vrf_id);
	}

	if(zclient->sock > 0) {
		zebra_message_send(zclient, command, vrf_id);
	}
}

static void zclient_event(enum event event, struct zclient *zclient) {
	switch(event) {
		case ZCLIENT_SCHEDULE:
			if(!zclient->t_connect) {
				zclient->t_connect = thread_add_event(zclient->master, zclient_connect, zclient, 0);
			}
			break;
		case ZCLIENT_CONNECT:
			if(zclient->fail >= 10) {
				return;
			}
			if(zclient_debug) {
				zlog_debug("zclient connect schedule interval is %d", zclient->fail < 3 ? 10 : 60);
			}
			if(!zclient->t_connect) {
				zclient->t_connect = thread_add_timer(zclient->master, zclient_connect, zclient, zclient->fail < 3 ? 10 : 60);
			}
			break;
		case ZCLIENT_READ: zclient->t_read = thread_add_read(zclient->master, zclient_read, zclient, zclient->sock); break;
	}
}

const char *zclient_serv_path_get() {
	return zclient_serv_path ? zclient_serv_path : ZEBRA_SERV_PATH;
}

void zclient_serv_path_set(char *path) {
	struct stat sb;

	/* reset */
	zclient_serv_path = NULL;

	/* test if `path' is socket. don't set it otherwise. */
	if(stat(path, &sb) == -1) {
		zlog_warn("%s: zebra socket `%s' does not exist", __func__, path);
		return;
	}

	if((sb.st_mode & S_IFMT) != S_IFSOCK) {
		zlog_warn("%s: `%s' is not unix socket, sir", __func__, path);
		return;
	}

	/* it seems that path is unix socket */
	zclient_serv_path = path;
}
