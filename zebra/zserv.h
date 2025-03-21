/* Zebra daemon server header.
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

#ifndef _ZEBRA_ZSERV_H
#define _ZEBRA_ZSERV_H

#include "if.h"
#include "rib.h"
#include "vrf.h"
#include "workqueue.h"

/* Default port information. */
#define ZEBRA_VTY_PORT 2601

/* Default configuration filename. */
#define DEFAULT_CONFIG_FILE "zebra.conf"

/* Client structure. */
struct zserv {
	/* Client file descriptor. */
	int sock;

	/* Input/output buffer to the client. */
	struct stream *ibuf;
	struct stream *obuf;

	/* Buffer of data waiting to be written to client. */
	struct buffer *wb;

	/* Threads for read/write. */
	struct thread *t_read;
	struct thread *t_write;

	/* Thread for delayed close. */
	struct thread *t_suicide;

	/* default routing table this client munges */
	int rtm_table;

	/* This client's redistribute flag. */
	vrf_bitmap_t redist[ZEBRA_ROUTE_MAX];

	/* Redistribute default route flag. */
	vrf_bitmap_t redist_default;

	/* Interface information. */
	vrf_bitmap_t ifinfo;

	/* Router-id information. */
	vrf_bitmap_t ridinfo;

	/* client's protocol */
	u_char proto;

	/* Statistics */
	u_int32_t redist_v4_add_cnt;
	u_int32_t redist_v4_del_cnt;
	u_int32_t redist_v6_add_cnt;
	u_int32_t redist_v6_del_cnt;
	u_int32_t v4_route_add_cnt;
	u_int32_t v4_route_upd8_cnt;
	u_int32_t v4_route_del_cnt;
	u_int32_t v6_route_add_cnt;
	u_int32_t v6_route_del_cnt;
	u_int32_t v6_route_upd8_cnt;
	u_int32_t connected_rt_add_cnt;
	u_int32_t connected_rt_del_cnt;
	u_int32_t ifup_cnt;
	u_int32_t ifdown_cnt;
	u_int32_t ifadd_cnt;
	u_int32_t ifdel_cnt;

	time_t connect_time;
	time_t last_read_time;
	time_t last_write_time;
	time_t nh_reg_time;
	time_t nh_dereg_time;
	time_t nh_last_upd_time;

	int last_read_cmd;
	int last_write_cmd;
};

/* Zebra instance */
struct zebra_t {
	/* Thread master */
	struct thread_master *master;
	struct list *client_list;

	/* default table */
	int rtm_table_default;

	/* rib work queue */
	struct work_queue *ribq;
	struct meta_queue *mq;
};

/* Prototypes. */
extern void zebra_init(void);
extern void zebra_if_init(void);
extern void zebra_zserv_socket_init(char *path);
extern void hostinfo_get(void);
extern void rib_init(void);
extern void interface_list(struct zebra_vrf *);
extern void route_read(struct zebra_vrf *);
extern void kernel_init(struct zebra_vrf *);
extern void kernel_terminate(struct zebra_vrf *);
extern void zebra_route_map_init(void);
extern void zebra_snmp_init(void);
extern void zebra_vty_init(void);

extern int zsend_interface_add(struct zserv *, struct interface *);
extern int zsend_interface_delete(struct zserv *, struct interface *);
extern int zsend_interface_address(int, struct zserv *, struct interface *, struct connected *);
extern int zsend_interface_update(int, struct zserv *, struct interface *);
extern int zsend_route_multipath(int, struct zserv *, struct prefix *, struct rib *);
extern int zsend_router_id_update(struct zserv *, struct prefix *, vrf_id_t);

extern int zsend_interface_link_params(struct zserv *, struct interface *);

extern pid_t pid;

extern void zserv_create_header(struct stream *s, uint16_t cmd, vrf_id_t);
extern int zebra_server_send_message(struct zserv *client);

#endif /* _ZEBRA_ZEBRA_H */
