/* RIP debug routines
 * Copyright (C) 1999 Kunihiro Ishiguro <kunihiro@zebra.org>
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
#include "command.h"
#include "ripd/rip_debug.h"

/* For debug statement. */
unsigned long rip_debug_event = 0;
unsigned long rip_debug_packet = 0;
unsigned long rip_debug_zebra = 0;

DEFUN(show_debugging_rip, show_debugging_rip_cmd, "show debugging rip", SHOW_STR DEBUG_STR RIP_STR) {
	vty_out(vty, "RIP debugging status:%s", VTY_NEWLINE);

	if(IS_RIP_DEBUG_EVENT) {
		vty_out(vty, "  RIP event debugging is on%s", VTY_NEWLINE);
	}

	if(IS_RIP_DEBUG_PACKET) {
		if(IS_RIP_DEBUG_SEND && IS_RIP_DEBUG_RECV) {
			vty_out(vty, "  RIP packet debugging is on%s", VTY_NEWLINE);
		} else {
			if(IS_RIP_DEBUG_SEND) {
				vty_out(vty, "  RIP packet send debugging is on%s", VTY_NEWLINE);
			} else {
				vty_out(vty, "  RIP packet receive debugging is on%s", VTY_NEWLINE);
			}
		}
	}

	if(IS_RIP_DEBUG_ZEBRA) {
		vty_out(vty, "  RIP zebra debugging is on%s", VTY_NEWLINE);
	}

	return CMD_SUCCESS;
}

DEFUN_WITH_NO(debug_rip_events, debug_rip_events_cmd, "debug rip events", DEBUG_STR RIP_STR "RIP events\n") {
	if(IS_NO){
		rip_debug_event = 0;
	} else {
		rip_debug_event = RIP_DEBUG_EVENT;
	}
	return CMD_WARNING;
}

DEFUN_WITH_NO(debug_rip_packet, debug_rip_packet_cmd, "debug rip packet", DEBUG_STR RIP_STR "RIP packet\n") {
	if(IS_NO){
		rip_debug_packet = 0;
	} else {
		rip_debug_packet = RIP_DEBUG_PACKET;
		rip_debug_packet |= RIP_DEBUG_SEND;
		rip_debug_packet |= RIP_DEBUG_RECV;
	}
	return CMD_SUCCESS;
}

DEFUN(debug_rip_packet_direct, debug_rip_packet_direct_cmd, "debug rip packet (recv|send)",
	DEBUG_STR RIP_STR "RIP packet\n"
			"RIP receive packet\n"
			"RIP send packet\n") {

	if(IS_NO){
		if(strncmp("send", argv[0], strlen(argv[0])) == 0) {
			if(IS_RIP_DEBUG_RECV) {
				rip_debug_packet &= ~RIP_DEBUG_SEND;
			} else {
				rip_debug_packet = 0;
			}
		} else if(strncmp("recv", argv[0], strlen(argv[0])) == 0) {
			if(IS_RIP_DEBUG_SEND) {
				rip_debug_packet &= ~RIP_DEBUG_RECV;
			} else {
				rip_debug_packet = 0;
			}
		}
	} else {
		rip_debug_packet |= RIP_DEBUG_PACKET;
		if(strncmp("send", argv[0], strlen(argv[0])) == 0) {
			rip_debug_packet |= RIP_DEBUG_SEND;
		}
		if(strncmp("recv", argv[0], strlen(argv[0])) == 0) {
			rip_debug_packet |= RIP_DEBUG_RECV;
		}
	}
	return CMD_SUCCESS;
}

/* N.B. the "detail" modifier is a no-op.  we leave this command
   for legacy compatibility. */
DEFUN_DEPRECATED(
	debug_rip_packet_detail, debug_rip_packet_detail_cmd, "debug rip packet (recv|send) detail",
	DEBUG_STR RIP_STR "RIP packet\n"
			  "RIP receive packet\n"
			  "RIP send packet\n"
			  "Detailed information display\n"
) {
	rip_debug_packet |= RIP_DEBUG_PACKET;
	if(strncmp("send", argv[0], strlen(argv[0])) == 0) {
		rip_debug_packet |= RIP_DEBUG_SEND;
	}
	if(strncmp("recv", argv[0], strlen(argv[0])) == 0) {
		rip_debug_packet |= RIP_DEBUG_RECV;
	}
	return CMD_SUCCESS;
}

DEFUN(debug_rip_zebra, debug_rip_zebra_cmd, "debug rip zebra", DEBUG_STR RIP_STR "RIP and ZEBRA communication\n") {
	rip_debug_zebra = RIP_DEBUG_ZEBRA;
	return CMD_WARNING;
}

DEFUN(no_debug_rip_zebra, no_debug_rip_zebra_cmd, "no debug rip zebra", NO_STR DEBUG_STR RIP_STR "RIP and ZEBRA communication\n") {
	rip_debug_zebra = 0;
	return CMD_WARNING;
}

/* Debug node. */
static struct cmd_node debug_node = { DEBUG_NODE, "", /* Debug node has no interface. */
				      1 };

static int config_write_debug(struct vty *vty) {
	int write = 0;

	if(IS_RIP_DEBUG_EVENT) {
		vty_out(vty, "debug rip events%s", VTY_NEWLINE);
		write++;
	}
	if(IS_RIP_DEBUG_PACKET) {
		if(IS_RIP_DEBUG_SEND && IS_RIP_DEBUG_RECV) {
			vty_out(vty, "debug rip packet%s", VTY_NEWLINE);
			write++;
		} else {
			if(IS_RIP_DEBUG_SEND) {
				vty_out(vty, "debug rip packet send%s", VTY_NEWLINE);
			} else {
				vty_out(vty, "debug rip packet recv%s", VTY_NEWLINE);
			}
			write++;
		}
	}
	if(IS_RIP_DEBUG_ZEBRA) {
		vty_out(vty, "debug rip zebra%s", VTY_NEWLINE);
		write++;
	}
	return write;
}

void rip_debug_reset(void) {
	rip_debug_event = 0;
	rip_debug_packet = 0;
	rip_debug_zebra = 0;
}

void rip_debug_init(void) {
	rip_debug_event = 0;
	rip_debug_packet = 0;
	rip_debug_zebra = 0;

	install_node(&debug_node, config_write_debug);

	install_element(ENABLE_NODE, &show_debugging_rip_cmd);
	install_element_with_no(ENABLE_NODE, &debug_rip_events_cmd);
	install_element_with_no(ENABLE_NODE, &debug_rip_packet_cmd);
//	install_element_with_no(ENABLE_NODE, &debug_rip_packet_direct_cmd);
	install_element(ENABLE_NODE, &debug_rip_packet_detail_cmd);
	install_element(ENABLE_NODE, &debug_rip_zebra_cmd);
	install_element(ENABLE_NODE, &no_debug_rip_zebra_cmd);

	install_element_with_no(CONFIG_NODE, &debug_rip_events_cmd);
	install_element_with_no(CONFIG_NODE, &debug_rip_packet_cmd);
//	install_element_with_no(CONFIG_NODE, &debug_rip_packet_direct_cmd);
	install_element(CONFIG_NODE, &debug_rip_packet_detail_cmd);
	install_element(CONFIG_NODE, &debug_rip_zebra_cmd);
	install_element(CONFIG_NODE, &no_debug_rip_zebra_cmd);
}
