/* Virtual terminal interface shell.
 * Copyright (C) 2000 Kunihiro Ishiguro
 * (C)2024 Hikaru Yamatohimemiya
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

#ifndef VTYSH_H
#define VTYSH_H

#define VTYSH_ZEBRA 0x01
#define VTYSH_RIPD 0x02
#define VTYSH_RIPNGD 0x04
#define VTYSH_OSPFD 0x08
#define VTYSH_OSPF6D 0x10
#define VTYSH_BGPD 0x20
#define VTYSH_ISISD 0x40
#define VTYSH_BABELD 0x80
#define VTYSH_PIMD 0x100
#define VTYSH_NHRPD 0x200
#define VTYSH_ALL VTYSH_ZEBRA | VTYSH_RIPD | VTYSH_RIPNGD | VTYSH_OSPFD | VTYSH_OSPF6D | VTYSH_BGPD | VTYSH_ISISD | VTYSH_BABELD | VTYSH_PIMD | VTYSH_NHRPD
#define VTYSH_RMAP VTYSH_ZEBRA | VTYSH_RIPD | VTYSH_RIPNGD | VTYSH_OSPFD | VTYSH_OSPF6D | VTYSH_BGPD | VTYSH_BABELD
#define VTYSH_INTERFACE VTYSH_ZEBRA | VTYSH_RIPD | VTYSH_RIPNGD | VTYSH_OSPFD | VTYSH_OSPF6D | VTYSH_ISISD | VTYSH_BABELD | VTYSH_PIMD | VTYSH_NHRPD

/* vtysh local configuration file. */
#define VTYSH_DEFAULT_CONFIG "vtysh.conf"

void vtysh_init_vty(void);
void vtysh_init_cmd(void);
extern int vtysh_connect_all(const char *optional_daemon_name);
void vtysh_readline_init(void);
void vtysh_user_init(void);

int vtysh_execute(const char *);
int vtysh_execute_no_pager(const char *);

char *vtysh_prompt(void);

void vtysh_config_write(void);

int vtysh_config_from_file(struct vty *, FILE *);

int vtysh_read_config(char *);

void vtysh_config_parse(char *);

void vtysh_config_dump(FILE *);

void vtysh_config_init(void);

void vtysh_pager_init(void);

/* Child process execution flag. */
extern int execute_flag;

extern struct vty *vty;

#endif /* VTYSH_H */
