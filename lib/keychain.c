/* key-chain for authentication.
   Copyright (C) 2000 Kunihiro Ishiguro
   (C)2024-2025 Hikaru Yamatohimemiya

This file is part of GNU Zebra.

GNU Zebra is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2, or (at your
option) any later version.

GNU Zebra is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Zebra; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include <zebra.h>

#include "command.h"
#include "memory.h"
#include "linklist.h"
#include "keychain.h"

/* Master list of key chain. */
struct list *keychain_list;

static struct keychain *keychain_new(void) {
	return XCALLOC(MTYPE_KEYCHAIN, sizeof(struct keychain));
}

static void keychain_free(struct keychain *keychain) {
	XFREE(MTYPE_KEYCHAIN, keychain);
}

static struct key *key_new(void) {
	return XCALLOC(MTYPE_KEY, sizeof(struct key));
}

static void key_free(struct key *key) {
	XFREE(MTYPE_KEY, key);
}

struct keychain *keychain_lookup(const char *name) {
	struct listnode *node;
	struct keychain *keychain;

	if(name == NULL) {
		return NULL;
	}

	for(ALL_LIST_ELEMENTS_RO(keychain_list, node, keychain)) {
		if(strcmp(keychain->name, name) == 0) {
			return keychain;
		}
	}
	return NULL;
}

static int key_cmp_func(void *arg1, void *arg2) {
	const struct key *k1 = arg1;
	const struct key *k2 = arg2;

	if(k1->index > k2->index) {
		return 1;
	}
	if(k1->index < k2->index) {
		return -1;
	}
	return 0;
}

static void key_delete_func(struct key *key) {
	if(key->string) {
		free(key->string);
	}
	key_free(key);
}

static struct keychain *keychain_get(const char *name) {
	struct keychain *keychain;

	keychain = keychain_lookup(name);

	if(keychain) {
		return keychain;
	}

	keychain = keychain_new();
	keychain->name = strdup(name);
	keychain->key = list_new();
	keychain->key->cmp = (int (*)(void *, void *)) key_cmp_func;
	keychain->key->del = (void (*)(void *)) key_delete_func;
	listnode_add(keychain_list, keychain);

	return keychain;
}

static void keychain_delete(struct keychain *keychain) {
	if(keychain->name) {
		free(keychain->name);
	}

	list_delete(keychain->key);
	listnode_delete(keychain_list, keychain);
	keychain_free(keychain);
}

static struct key *key_lookup(const struct keychain *keychain, u_int32_t index) {
	struct listnode *node;
	struct key *key;

	for(ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if(key->index == index) {
			return key;
		}
	}
	return NULL;
}

struct key *key_lookup_for_accept(const struct keychain *keychain, u_int32_t index) {
	struct listnode *node;
	struct key *key;
	time_t now;

	now = time(NULL);

	for(ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if(key->index >= index) {
			if(key->accept.start == 0) {
				return key;
			}

			if(key->accept.start <= now) {
				if(key->accept.end >= now || key->accept.end == -1) {
					return key;
				}
			}
		}
	}
	return NULL;
}

struct key *key_match_for_accept(const struct keychain *keychain, const char *auth_str) {
	struct listnode *node;
	struct key *key;
	time_t now;

	now = time(NULL);

	for(ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if(key->accept.start == 0 || (key->accept.start <= now && (key->accept.end >= now || key->accept.end == -1))) {
			if(strncmp(key->string, auth_str, 16) == 0) {
				return key;
			}
		}
	}
	return NULL;
}

struct key *key_lookup_for_send(const struct keychain *keychain) {
	struct listnode *node;
	struct key *key;
	time_t now;

	now = time(NULL);

	for(ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if(key->send.start == 0) {
			return key;
		}

		if(key->send.start <= now) {
			if(key->send.end >= now || key->send.end == -1) {
				return key;
			}
		}
	}
	return NULL;
}

static struct key *key_get(const struct keychain *keychain, u_int32_t index) {
	struct key *key;

	key = key_lookup(keychain, index);

	if(key) {
		return key;
	}

	key = key_new();
	key->index = index;
	listnode_add_sort(keychain->key, key);

	return key;
}

static void key_delete(struct keychain *keychain, struct key *key) {
	listnode_delete(keychain->key, key);

	if(key->string) {
		free(key->string);
	}
	key_free(key);
}

DEFUN(key_chain, key_chain_cmd, "key chain WORD",
      "Authentication key management\n"
      "Key-chain management\n"
      "Key-chain name\n") {
	struct keychain *keychain;

	keychain = keychain_get(argv[0]);
	vty->index = keychain;
	vty->node = KEYCHAIN_NODE;

	return CMD_SUCCESS;
}

DEFUN(no_key_chain, no_key_chain_cmd, "no key chain WORD",
      NO_STR "Authentication key management\n"
	     "Key-chain management\n"
	     "Key-chain name\n") {
	struct keychain *keychain;

	keychain = keychain_lookup(argv[0]);

	if(!keychain) {
		vty_out(vty, "Can't find keychain %s%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	keychain_delete(keychain);

	return CMD_SUCCESS;
}

DEFUN(key, key_cmd, "key <0-2147483647>",
      "Configure a key\n"
      "Key identifier number\n") {
	struct keychain *keychain;
	struct key *key;
	u_int32_t index;

	keychain = vty->index;

	VTY_GET_INTEGER("key identifier", index, argv[0]);
	key = key_get(keychain, index);
	vty->index_sub = key;
	vty->node = KEYCHAIN_KEY_NODE;

	return CMD_SUCCESS;
}

DEFUN(no_key, no_key_cmd, "no key <0-2147483647>",
      NO_STR "Delete a key\n"
	     "Key identifier number\n") {
	struct keychain *keychain;
	struct key *key;
	u_int32_t index;

	keychain = vty->index;

	VTY_GET_INTEGER("key identifier", index, argv[0]);
	key = key_lookup(keychain, index);
	if(!key) {
		vty_out(vty, "Can't find key %d%s", index, VTY_NEWLINE);
		return CMD_WARNING;
	}

	key_delete(keychain, key);

	vty->node = KEYCHAIN_NODE;

	return CMD_SUCCESS;
}

DEFUN(key_string, key_string_cmd, "key-string LINE",
      "Set key string\n"
      "The key\n") {
	struct key *key;

	key = vty->index_sub;

	if(key->string) {
		free(key->string);
	}
	key->string = strdup(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(no_key_string, no_key_string_cmd, "no key-string [LINE]",
      NO_STR "Unset key string\n"
	     "The key\n") {
	struct key *key;

	key = vty->index_sub;

	if(key->string) {
		free(key->string);
		key->string = NULL;
	}

	return CMD_SUCCESS;
}

/* Convert HH:MM:SS MON DAY YEAR to time_t value.  -1 is returned when
   given string is malformed. */
static time_t key_str2time(const char *time_str, const char *day_str, const char *month_str, const char *year_str) {
	int i = 0;
	char *colon;
	struct tm tm;
	time_t time;
	unsigned int sec, min, hour;
	unsigned int day, month, year;

	const char *month_name[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December", NULL };

#define _GET_LONG_RANGE(V, STR, MMCOND) \
	{ \
		unsigned long tmpl; \
		char *endptr = NULL; \
		tmpl = strtoul((STR), &endptr, 10); \
		if(*endptr != '\0' || tmpl == ULONG_MAX) return -1; \
		if(MMCOND) return -1; \
		(V) = tmpl; \
	}
#define GET_LONG_RANGE(V, STR, MIN, MAX) _GET_LONG_RANGE(V, STR, tmpl<(MIN) || tmpl>(MAX))
#define GET_LONG_RANGE0(V, STR, MAX) _GET_LONG_RANGE(V, STR, tmpl > (MAX))

	/* Check hour field of time_str. */
	colon = strchr(time_str, ':');
	if(colon == NULL) {
		return -1;
	}
	*colon = '\0';

	/* Hour must be between 0 and 23. */
	GET_LONG_RANGE0(hour, time_str, 23);

	/* Check min field of time_str. */
	time_str = colon + 1;
	colon = strchr(time_str, ':');
	if(*time_str == '\0' || colon == NULL) {
		return -1;
	}
	*colon = '\0';

	/* Min must be between 0 and 59. */
	GET_LONG_RANGE0(min, time_str, 59);

	/* Check sec field of time_str. */
	time_str = colon + 1;
	if(*time_str == '\0') {
		return -1;
	}

	/* Sec must be between 0 and 59. */
	GET_LONG_RANGE0(sec, time_str, 59);

	/* Check day_str.  Day must be <1-31>. */
	GET_LONG_RANGE(day, day_str, 1, 31);

	/* Check month_str.  Month must match month_name. */
	month = 0;
	if(strlen(month_str) >= 3) {
		for(i = 0; month_name[i]; i++) {
			if(strncmp(month_str, month_name[i], strlen(month_str)) == 0) {
				month = i;
				break;
			}
		}
	}
	if(!month_name[i]) {
		return -1;
	}

	/* Check year_str.  Year must be <1993-2035>. */
	GET_LONG_RANGE(year, year_str, 1993, 2035);

	memset(&tm, 0, sizeof(struct tm));
	tm.tm_sec = sec;
	tm.tm_min = min;
	tm.tm_hour = hour;
	tm.tm_mon = month;
	tm.tm_mday = day;
	tm.tm_year = year - 1900;

	time = mktime(&tm);

	return time;
#undef GET_LONG_RANGE
}

static int key_lifetime_set(
	struct vty *vty, struct key_range *krange, const char *stime_str, const char *sday_str, const char *smonth_str, const char *syear_str, const char *etime_str, const char *eday_str, const char *emonth_str, const char *eyear_str
) {
	time_t time_start;
	time_t time_end;

	time_start = key_str2time(stime_str, sday_str, smonth_str, syear_str);
	if(time_start < 0) {
		vty_out(vty, "Malformed time value%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	time_end = key_str2time(etime_str, eday_str, emonth_str, eyear_str);

	if(time_end < 0) {
		vty_out(vty, "Malformed time value%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if(time_end <= time_start) {
		vty_out(vty, "Expire time is not later than start time%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	krange->start = time_start;
	krange->end = time_end;

	return CMD_SUCCESS;
}

static int key_lifetime_duration_set(struct vty *vty, struct key_range *krange, const char *stime_str, const char *sday_str, const char *smonth_str, const char *syear_str, const char *duration_str) {
	time_t time_start;
	u_int32_t duration;

	time_start = key_str2time(stime_str, sday_str, smonth_str, syear_str);
	if(time_start < 0) {
		vty_out(vty, "Malformed time value%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	krange->start = time_start;

	VTY_GET_INTEGER("duration", duration, duration_str);
	krange->duration = 1;
	krange->end = time_start + duration;

	return CMD_SUCCESS;
}

static int key_lifetime_infinite_set(struct vty *vty, struct key_range *krange, const char *stime_str, const char *sday_str, const char *smonth_str, const char *syear_str) {
	time_t time_start;

	time_start = key_str2time(stime_str, sday_str, smonth_str, syear_str);
	if(time_start < 0) {
		vty_out(vty, "Malformed time value%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	krange->start = time_start;

	krange->end = -1;

	return CMD_SUCCESS;
}

DEFUN(accept_lifetime_day_month_day_month, accept_lifetime_day_month_day_month_cmd, "accept-lifetime HH:MM:SS <1-31> MONTH <1993-2035> HH:MM:SS <1-31> MONTH <1993-2035>",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Day of th month to expire\n"
      "Month of the year to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->accept, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
}

DEFUN(accept_lifetime_day_month_month_day, accept_lifetime_day_month_month_day_cmd, "accept-lifetime HH:MM:SS <1-31> MONTH <1993-2035> HH:MM:SS MONTH <1-31> <1993-2035>",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Month of the year to expire\n"
      "Day of th month to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->accept, argv[0], argv[1], argv[2], argv[3], argv[4], argv[6], argv[5], argv[7]);
}

DEFUN(accept_lifetime_month_day_day_month, accept_lifetime_month_day_day_month_cmd, "accept-lifetime HH:MM:SS MONTH <1-31> <1993-2035> HH:MM:SS <1-31> MONTH <1993-2035>",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Day of th month to expire\n"
      "Month of the year to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->accept, argv[0], argv[2], argv[1], argv[3], argv[4], argv[5], argv[6], argv[7]);
}

DEFUN(accept_lifetime_month_day_month_day, accept_lifetime_month_day_month_day_cmd, "accept-lifetime HH:MM:SS MONTH <1-31> <1993-2035> HH:MM:SS MONTH <1-31> <1993-2035>",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Month of the year to expire\n"
      "Day of th month to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->accept, argv[0], argv[2], argv[1], argv[3], argv[4], argv[6], argv[5], argv[7]);
}

DEFUN(accept_lifetime_infinite_day_month, accept_lifetime_infinite_day_month_cmd, "accept-lifetime HH:MM:SS <1-31> MONTH <1993-2035> infinite",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Never expires") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_infinite_set(vty, &key->accept, argv[0], argv[1], argv[2], argv[3]);
}

DEFUN(accept_lifetime_infinite_month_day, accept_lifetime_infinite_month_day_cmd, "accept-lifetime HH:MM:SS MONTH <1-31> <1993-2035> infinite",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Never expires") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_infinite_set(vty, &key->accept, argv[0], argv[2], argv[1], argv[3]);
}

DEFUN(accept_lifetime_duration_day_month, accept_lifetime_duration_day_month_cmd, "accept-lifetime HH:MM:SS <1-31> MONTH <1993-2035> duration <1-2147483646>",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Duration of the key\n"
      "Duration seconds\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_duration_set(vty, &key->accept, argv[0], argv[1], argv[2], argv[3], argv[4]);
}

DEFUN(accept_lifetime_duration_month_day, accept_lifetime_duration_month_day_cmd, "accept-lifetime HH:MM:SS MONTH <1-31> <1993-2035> duration <1-2147483646>",
      "Set accept lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Duration of the key\n"
      "Duration seconds\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_duration_set(vty, &key->accept, argv[0], argv[2], argv[1], argv[3], argv[4]);
}

DEFUN(send_lifetime_day_month_day_month, send_lifetime_day_month_day_month_cmd, "send-lifetime HH:MM:SS <1-31> MONTH <1993-2035> HH:MM:SS <1-31> MONTH <1993-2035>",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Day of th month to expire\n"
      "Month of the year to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->send, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
}

DEFUN(send_lifetime_day_month_month_day, send_lifetime_day_month_month_day_cmd, "send-lifetime HH:MM:SS <1-31> MONTH <1993-2035> HH:MM:SS MONTH <1-31> <1993-2035>",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Month of the year to expire\n"
      "Day of th month to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->send, argv[0], argv[1], argv[2], argv[3], argv[4], argv[6], argv[5], argv[7]);
}

DEFUN(send_lifetime_month_day_day_month, send_lifetime_month_day_day_month_cmd, "send-lifetime HH:MM:SS MONTH <1-31> <1993-2035> HH:MM:SS <1-31> MONTH <1993-2035>",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Day of th month to expire\n"
      "Month of the year to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->send, argv[0], argv[2], argv[1], argv[3], argv[4], argv[5], argv[6], argv[7]);
}

DEFUN(send_lifetime_month_day_month_day, send_lifetime_month_day_month_day_cmd, "send-lifetime HH:MM:SS MONTH <1-31> <1993-2035> HH:MM:SS MONTH <1-31> <1993-2035>",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Time to expire\n"
      "Month of the year to expire\n"
      "Day of th month to expire\n"
      "Year to expire\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_set(vty, &key->send, argv[0], argv[2], argv[1], argv[3], argv[4], argv[6], argv[5], argv[7]);
}

DEFUN(send_lifetime_infinite_day_month, send_lifetime_infinite_day_month_cmd, "send-lifetime HH:MM:SS <1-31> MONTH <1993-2035> infinite",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Never expires") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_infinite_set(vty, &key->send, argv[0], argv[1], argv[2], argv[3]);
}

DEFUN(send_lifetime_infinite_month_day, send_lifetime_infinite_month_day_cmd, "send-lifetime HH:MM:SS MONTH <1-31> <1993-2035> infinite",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Never expires") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_infinite_set(vty, &key->send, argv[0], argv[2], argv[1], argv[3]);
}

DEFUN(send_lifetime_duration_day_month, send_lifetime_duration_day_month_cmd, "send-lifetime HH:MM:SS <1-31> MONTH <1993-2035> duration <1-2147483646>",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Day of th month to start\n"
      "Month of the year to start\n"
      "Year to start\n"
      "Duration of the key\n"
      "Duration seconds\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_duration_set(vty, &key->send, argv[0], argv[1], argv[2], argv[3], argv[4]);
}

DEFUN(send_lifetime_duration_month_day, send_lifetime_duration_month_day_cmd, "send-lifetime HH:MM:SS MONTH <1-31> <1993-2035> duration <1-2147483646>",
      "Set send lifetime of the key\n"
      "Time to start\n"
      "Month of the year to start\n"
      "Day of th month to start\n"
      "Year to start\n"
      "Duration of the key\n"
      "Duration seconds\n") {
	struct key *key;

	key = vty->index_sub;

	return key_lifetime_duration_set(vty, &key->send, argv[0], argv[2], argv[1], argv[3], argv[4]);
}

static struct cmd_node keychain_node = { KEYCHAIN_NODE, "%s(config-keychain)# ", 1 };

static struct cmd_node keychain_key_node = { KEYCHAIN_KEY_NODE, "%s(config-keychain-key)# ", 1 };

static int keychain_strftime(char *buf, int bufsiz, time_t *time) {
	struct tm *tm;
	size_t len;

	tm = localtime(time);

	len = strftime(buf, bufsiz, "%T %b %d %Y", tm);

	return len;
}

static int keychain_config_write(struct vty *vty) {
	struct keychain *keychain;
	struct key *key;
	struct listnode *node;
	struct listnode *knode;
	char buf[BUFSIZ];

	for(ALL_LIST_ELEMENTS_RO(keychain_list, node, keychain)) {
		vty_out(vty, "key chain %s%s", keychain->name, VTY_NEWLINE);

		for(ALL_LIST_ELEMENTS_RO(keychain->key, knode, key)) {
			vty_out(vty, " key %d%s", key->index, VTY_NEWLINE);

			if(key->string) {
				vty_out(vty, "  key-string %s%s", key->string, VTY_NEWLINE);
			}

			if(key->accept.start) {
				keychain_strftime(buf, BUFSIZ, &key->accept.start);
				vty_out(vty, "  accept-lifetime %s", buf);

				if(key->accept.end == -1) {
					vty_out(vty, " infinite");
				} else if(key->accept.duration) {
					vty_out(vty, " duration %ld", (long) (key->accept.end - key->accept.start));
				} else {
					keychain_strftime(buf, BUFSIZ, &key->accept.end);
					vty_out(vty, " %s", buf);
				}
				vty_out(vty, "%s", VTY_NEWLINE);
			}

			if(key->send.start) {
				keychain_strftime(buf, BUFSIZ, &key->send.start);
				vty_out(vty, "  send-lifetime %s", buf);

				if(key->send.end == -1) {
					vty_out(vty, " infinite");
				} else if(key->send.duration) {
					vty_out(vty, " duration %ld", (long) (key->send.end - key->send.start));
				} else {
					keychain_strftime(buf, BUFSIZ, &key->send.end);
					vty_out(vty, " %s", buf);
				}
				vty_out(vty, "%s", VTY_NEWLINE);
			}
		}
		vty_out(vty, "!%s", VTY_NEWLINE);
	}

	return 0;
}

void keychain_init() {
	keychain_list = list_new();

	install_node(&keychain_node, keychain_config_write);
	install_node(&keychain_key_node, NULL);

	install_default(KEYCHAIN_NODE);
	install_default(KEYCHAIN_KEY_NODE);

	install_element(CONFIG_NODE, &key_chain_cmd);
	install_element(CONFIG_NODE, &no_key_chain_cmd);
	install_element(KEYCHAIN_NODE, &key_cmd);
	install_element(KEYCHAIN_NODE, &no_key_cmd);

	install_element(KEYCHAIN_NODE, &key_chain_cmd);
	install_element(KEYCHAIN_NODE, &no_key_chain_cmd);

	install_element(KEYCHAIN_KEY_NODE, &key_string_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_key_string_cmd);

	install_element(KEYCHAIN_KEY_NODE, &key_chain_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_key_chain_cmd);

	install_element(KEYCHAIN_KEY_NODE, &key_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_key_cmd);

	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_day_month_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_day_month_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_month_day_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_month_day_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_infinite_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_infinite_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_duration_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &accept_lifetime_duration_month_day_cmd);

	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_day_month_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_day_month_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_month_day_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_month_day_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_infinite_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_infinite_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_duration_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE, &send_lifetime_duration_month_day_cmd);
}
