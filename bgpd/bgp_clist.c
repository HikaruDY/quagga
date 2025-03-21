/* BGP community-list and extcommunity-list.
   Copyright (C) 1999 Kunihiro Ishiguro
   (C)2024-2025 Hikaru Yamatohimemiya

This file is part of GNU Zebra.

GNU Zebra is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

GNU Zebra is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Zebra; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include <zebra.h>

#include "command.h"
#include "prefix.h"
#include "memory.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_regex.h"
#include "bgpd/bgp_clist.h"

/* Lookup master structure for community-list or
   extcommunity-list.  */
struct community_list_master *community_list_master_lookup(struct community_list_handler *ch, int master) {
	if(ch) {
		switch(master) {
			case COMMUNITY_LIST_MASTER: return &ch->community_list;
			case EXTCOMMUNITY_LIST_MASTER: return &ch->extcommunity_list;
			case LARGE_COMMUNITY_LIST_MASTER: return &ch->lcommunity_list;
		}
	}
	return NULL;
}

/* Allocate a new community list entry.  */
static struct community_entry *community_entry_new(void) {
	return XCALLOC(MTYPE_COMMUNITY_LIST_ENTRY, sizeof(struct community_entry));
}

/* Free community list entry.  */
static void community_entry_free(struct community_entry *entry) {
	switch(entry->style) {
		case COMMUNITY_LIST_STANDARD:
			if(entry->u.com) {
				community_free(entry->u.com);
			}
			break;
		case LARGE_COMMUNITY_LIST_STANDARD:
			if(entry->u.lcom) {
				lcommunity_free(&entry->u.lcom);
			}
			break;
		case EXTCOMMUNITY_LIST_STANDARD:
			/* In case of standard extcommunity-list, configuration string
         is made by ecommunity_ecom2str().  */
			if(entry->config) {
				XFREE(MTYPE_ECOMMUNITY_STR, entry->config);
			}
			if(entry->u.ecom) {
				ecommunity_free(&entry->u.ecom);
			}
			break;
		case COMMUNITY_LIST_EXPANDED:
		case EXTCOMMUNITY_LIST_EXPANDED:
		case LARGE_COMMUNITY_LIST_EXPANDED:
			if(entry->config) {
				XFREE(MTYPE_COMMUNITY_LIST_CONFIG, entry->config);
			}
			if(entry->reg) {
				bgp_regex_free(entry->reg);
			}
		default: break;
	}
	XFREE(MTYPE_COMMUNITY_LIST_ENTRY, entry);
}

/* Allocate a new community-list.  */
static struct community_list *community_list_new(void) {
	return XCALLOC(MTYPE_COMMUNITY_LIST, sizeof(struct community_list));
}

/* Free community-list.  */
static void community_list_free(struct community_list *list) {
	if(list->name) {
		XFREE(MTYPE_COMMUNITY_LIST_NAME, list->name);
	}
	XFREE(MTYPE_COMMUNITY_LIST, list);
}

static struct community_list *community_list_insert(struct community_list_handler *ch, const char *name, int master) {
	size_t i;
	long number;
	struct community_list *new;
	struct community_list *point;
	struct community_list_list *list;
	struct community_list_master *cm;

	/* Lookup community-list master.  */
	cm = community_list_master_lookup(ch, master);
	if(!cm) {
		return NULL;
	}

	/* Allocate new community_list and copy given name. */
	new = community_list_new();
	new->name = XSTRDUP(MTYPE_COMMUNITY_LIST_NAME, name);

	/* If name is made by all digit character.  We treat it as
     number. */
	for(number = 0, i = 0; i < strlen(name); i++) {
		if(isdigit((int) name[i])) {
			number = (number * 10) + (name[i] - '0');
		} else {
			break;
		}
	}

	/* In case of name is all digit character */
	if(i == strlen(name)) {
		new->sort = COMMUNITY_LIST_NUMBER;

		/* Set access_list to number list. */
		list = &cm->num;

		for(point = list->head; point; point = point->next) {
			if(atol(point->name) >= number) {
				break;
			}
		}
	} else {
		new->sort = COMMUNITY_LIST_STRING;

		/* Set access_list to string list. */
		list = &cm->str;

		/* Set point to insertion point. */
		for(point = list->head; point; point = point->next) {
			if(strcmp(point->name, name) >= 0) {
				break;
			}
		}
	}

	/* Link to upper list.  */
	new->parent = list;

	/* In case of this is the first element of master. */
	if(list->head == NULL) {
		list->head = list->tail = new;
		return new;
	}

	/* In case of insertion is made at the tail of access_list. */
	if(point == NULL) {
		new->prev = list->tail;
		list->tail->next = new;
		list->tail = new;
		return new;
	}

	/* In case of insertion is made at the head of access_list. */
	if(point == list->head) {
		new->next = list->head;
		list->head->prev = new;
		list->head = new;
		return new;
	}

	/* Insertion is made at middle of the access_list. */
	new->next = point;
	new->prev = point->prev;

	if(point->prev) {
		point->prev->next = new;
	}
	point->prev = new;

	return new;
}

struct community_list *community_list_lookup(struct community_list_handler *ch, const char *name, int master) {
	struct community_list *list;
	struct community_list_master *cm;

	if(!name) {
		return NULL;
	}

	cm = community_list_master_lookup(ch, master);
	if(!cm) {
		return NULL;
	}

	for(list = cm->num.head; list; list = list->next) {
		if(strcmp(list->name, name) == 0) {
			return list;
		}
	}
	for(list = cm->str.head; list; list = list->next) {
		if(strcmp(list->name, name) == 0) {
			return list;
		}
	}

	return NULL;
}

static struct community_list *community_list_get(struct community_list_handler *ch, const char *name, int master) {
	struct community_list *list;

	list = community_list_lookup(ch, name, master);
	if(!list) {
		list = community_list_insert(ch, name, master);
	}
	return list;
}

static void community_list_delete(struct community_list *list) {
	struct community_list_list *clist;
	struct community_entry *entry, *next;

	for(entry = list->head; entry; entry = next) {
		next = entry->next;
		community_entry_free(entry);
	}

	clist = list->parent;

	if(list->next) {
		list->next->prev = list->prev;
	} else {
		clist->tail = list->prev;
	}

	if(list->prev) {
		list->prev->next = list->next;
	} else {
		clist->head = list->next;
	}

	community_list_free(list);
}

static int community_list_empty_p(struct community_list *list) {
	return (list->head == NULL && list->tail == NULL) ? 1 : 0;
}

/* Add community-list entry to the list.  */
static void community_list_entry_add(struct community_list *list, struct community_entry *entry) {
	entry->next = NULL;
	entry->prev = list->tail;

	if(list->tail) {
		list->tail->next = entry;
	} else {
		list->head = entry;
	}
	list->tail = entry;
}

/* Delete community-list entry from the list.  */
static void community_list_entry_delete(struct community_list *list, struct community_entry *entry, int style) {
	if(entry->next) {
		entry->next->prev = entry->prev;
	} else {
		list->tail = entry->prev;
	}

	if(entry->prev) {
		entry->prev->next = entry->next;
	} else {
		list->head = entry->next;
	}

	community_entry_free(entry);

	if(community_list_empty_p(list)) {
		community_list_delete(list);
	}
}

/* Lookup community-list entry from the list.  */
static struct community_entry *community_list_entry_lookup(struct community_list *list, const void *arg, int direct) {
	struct community_entry *entry;

	for(entry = list->head; entry; entry = entry->next) {
		switch(entry->style) {
			case COMMUNITY_LIST_STANDARD:
				if(community_cmp(entry->u.com, arg)) {
					return entry;
				}
				break;
			case LARGE_COMMUNITY_LIST_STANDARD:
				if(lcommunity_cmp(entry->u.lcom, arg)) {
					return entry;
				}
				break;
			case EXTCOMMUNITY_LIST_STANDARD:
				if(ecommunity_cmp(entry->u.ecom, arg)) {
					return entry;
				}
				break;
			case COMMUNITY_LIST_EXPANDED:
			case EXTCOMMUNITY_LIST_EXPANDED:
			case LARGE_COMMUNITY_LIST_EXPANDED:
				if(strcmp(entry->config, arg) == 0) {
					return entry;
				}
				break;
			default: break;
		}
	}
	return NULL;
}

static char *community_str_get(struct community *com, int i) {
	int len;
	u_int32_t comval;
	u_int16_t as;
	u_int16_t val;
	char *str;
	char *pnt;

	memcpy(&comval, com_nthval(com, i), sizeof(u_int32_t));
	comval = ntohl(comval);

	switch(comval) {
		case COMMUNITY_INTERNET: len = strlen(" internet"); break;
		case COMMUNITY_NO_EXPORT: len = strlen(" no-export"); break;
		case COMMUNITY_NO_ADVERTISE: len = strlen(" no-advertise"); break;
		case COMMUNITY_LOCAL_AS: len = strlen(" local-AS"); break;
		default: len = strlen(" 65536:65535"); break;
	}

	/* Allocate memory.  */
	str = pnt = XMALLOC(MTYPE_COMMUNITY_STR, len);

	switch(comval) {
		case COMMUNITY_INTERNET:
			strcpy(pnt, "internet");
			pnt += strlen("internet");
			break;
		case COMMUNITY_NO_EXPORT:
			strcpy(pnt, "no-export");
			pnt += strlen("no-export");
			break;
		case COMMUNITY_NO_ADVERTISE:
			strcpy(pnt, "no-advertise");
			pnt += strlen("no-advertise");
			break;
		case COMMUNITY_LOCAL_AS:
			strcpy(pnt, "local-AS");
			pnt += strlen("local-AS");
			break;
		default:
			as = (comval >> 16) & 0xFFFF;
			val = comval & 0xFFFF;
			sprintf(pnt, "%u:%d", as, val);
			pnt += strlen(pnt);
			break;
	}

	*pnt = '\0';

	return str;
}

/* Internal function to perform regular expression match for
 *  * a single community. */
static int community_regexp_include(regex_t *reg, struct community *com, int i) {
	char *str;
	int rv;

	/* When there is no communities attribute it is treated as empty
 *      string.  */
	if(com == NULL || com->size == 0) {
		str = XSTRDUP(MTYPE_COMMUNITY_STR, "");
	} else {
		str = community_str_get(com, i);
	}

	/* Regular expression match.  */
	rv = regexec(reg, str, 0, NULL, 0);

	XFREE(MTYPE_COMMUNITY_STR, str);

	if(rv == 0) {
		return 1;
	}

	/* No match.  */
	return 0;
}

/* Internal function to perform regular expression match for community
   attribute.  */
static int community_regexp_match(struct community *com, regex_t *reg) {
	const char *str;

	/* When there is no communities attribute it is treated as empty
     string.  */
	if(com == NULL || com->size == 0) {
		str = "";
	} else {
		str = community_str(com);
	}

	/* Regular expression match.  */
	if(regexec(reg, str, 0, NULL, 0) == 0) {
		return 1;
	}

	/* No match.  */
	return 0;
}

static char *lcommunity_str_get(struct lcommunity *lcom, int i) {
	struct lcommunity_val lcomval;
	u_int32_t globaladmin;
	u_int32_t localdata1;
	u_int32_t localdata2;
	char *str;
	u_char *ptr;
	char *pnt;

	ptr = lcom->val;
	ptr += (i * LCOMMUNITY_SIZE);

	memcpy(&lcomval, ptr, LCOMMUNITY_SIZE);

	/* Allocate memory.  48 bytes taken off bgp_lcommunity.c */
	str = pnt = XMALLOC(MTYPE_LCOMMUNITY_STR, 48);

	ptr = (u_char *) lcomval.val;
	globaladmin = (*ptr++ << 24);
	globaladmin |= (*ptr++ << 16);
	globaladmin |= (*ptr++ << 8);
	globaladmin |= (*ptr++);

	localdata1 = (*ptr++ << 24);
	localdata1 |= (*ptr++ << 16);
	localdata1 |= (*ptr++ << 8);
	localdata1 |= (*ptr++);

	localdata2 = (*ptr++ << 24);
	localdata2 |= (*ptr++ << 16);
	localdata2 |= (*ptr++ << 8);
	localdata2 |= (*ptr++);

	sprintf(pnt, "%u:%u:%u", globaladmin, localdata1, localdata2);
	pnt += strlen(pnt);
	*pnt = '\0';

	return str;
}

/* Internal function to perform regular expression match for
 *  * a single community. */
static int lcommunity_regexp_include(regex_t *reg, struct lcommunity *lcom, int i) {
	const char *str;

	/* When there is no communities attribute it is treated as empty
 *      string.  */
	if(lcom == NULL || lcom->size == 0) {
		str = "";
	} else {
		str = lcommunity_str_get(lcom, i);
	}

	/* Regular expression match.  */
	if(regexec(reg, str, 0, NULL, 0) == 0) {
		return 1;
	}

	/* No match.  */
	return 0;
}

static int lcommunity_regexp_match(struct lcommunity *com, regex_t *reg) {
	const char *str;

	/* When there is no communities attribute it is treated as empty
     string.  */
	if(com == NULL || com->size == 0) {
		str = "";
	} else {
		str = lcommunity_str(com);
	}

	/* Regular expression match.  */
	if(regexec(reg, str, 0, NULL, 0) == 0) {
		return 1;
	}

	/* No match.  */
	return 0;
}

static int ecommunity_regexp_match(struct ecommunity *ecom, regex_t *reg) {
	const char *str;

	/* When there is no communities attribute it is treated as empty
     string.  */
	if(ecom == NULL || ecom->size == 0) {
		str = "";
	} else {
		str = ecommunity_str(ecom);
	}

	/* Regular expression match.  */
	if(regexec(reg, str, 0, NULL, 0) == 0) {
		return 1;
	}

	/* No match.  */
	return 0;
}

/* When given community attribute matches to the community-list return
   1 else return 0.  */
int community_list_match(struct community *com, struct community_list *list) {
	struct community_entry *entry;

	for(entry = list->head; entry; entry = entry->next) {
		if(entry->any) {
			return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
		}

		if(entry->style == COMMUNITY_LIST_STANDARD) {
			if(community_include(entry->u.com, COMMUNITY_INTERNET)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}

			if(community_match(com, entry->u.com)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		} else if(entry->style == COMMUNITY_LIST_EXPANDED) {
			if(community_regexp_match(com, entry->reg)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		}
	}
	return 0;
}

int lcommunity_list_match(struct lcommunity *lcom, struct community_list *list) {
	struct community_entry *entry;

	for(entry = list->head; entry; entry = entry->next) {
		if(entry->any) {
			return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
		}

		if(entry->style == LARGE_COMMUNITY_LIST_STANDARD) {
			if(lcommunity_match(lcom, entry->u.lcom)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		} else if(entry->style == LARGE_COMMUNITY_LIST_EXPANDED) {
			if(lcommunity_regexp_match(lcom, entry->reg)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		}
	}
	return 0;
}

int ecommunity_list_match(struct ecommunity *ecom, struct community_list *list) {
	struct community_entry *entry;

	for(entry = list->head; entry; entry = entry->next) {
		if(entry->any) {
			return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
		}

		if(entry->style == EXTCOMMUNITY_LIST_STANDARD) {
			if(ecommunity_match(ecom, entry->u.ecom)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		} else if(entry->style == EXTCOMMUNITY_LIST_EXPANDED) {
			if(ecommunity_regexp_match(ecom, entry->reg)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		}
	}
	return 0;
}

/* Perform exact matching.  In case of expanded community-list, do
   same thing as community_list_match().  */
int community_list_exact_match(struct community *com, struct community_list *list) {
	struct community_entry *entry;

	for(entry = list->head; entry; entry = entry->next) {
		if(entry->any) {
			return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
		}

		if(entry->style == COMMUNITY_LIST_STANDARD) {
			if(community_include(entry->u.com, COMMUNITY_INTERNET)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}

			if(community_cmp(com, entry->u.com)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		} else if(entry->style == COMMUNITY_LIST_EXPANDED) {
			if(community_regexp_match(com, entry->reg)) {
				return entry->direct == COMMUNITY_PERMIT ? 1 : 0;
			}
		}
	}
	return 0;
}

/* Delete all permitted communities in the list from com.  */
struct community *community_list_match_delete(struct community *com, struct community_list *list) {
	struct community_entry *entry;
	u_int32_t val;
	u_int32_t com_index_to_delete[com->size];
	int delete_index = 0;
	int i;

	/* Loop over each community value and evaluate each against the
   * community-list.  If we need to delete a community value add its index to
   * com_index_to_delete.
   */
	for(i = 0; i < com->size; i++) {
		val = community_val_get(com, i);

		for(entry = list->head; entry; entry = entry->next) {
			if(entry->any) {
				if(entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}

			else if((entry->style == COMMUNITY_LIST_STANDARD) && (community_include(entry->u.com, COMMUNITY_INTERNET) || community_include(entry->u.com, val))) {
				if(entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}

			else if((entry->style == COMMUNITY_LIST_EXPANDED) && community_regexp_include(entry->reg, com, i)) {
				if(entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}
		}
	}

	/* Delete all of the communities we flagged for deletion */
	for(i = delete_index - 1; i >= 0; i--) {
		val = community_val_get(com, com_index_to_delete[i]);
		community_del_val(com, &val);
	}

	return com;
}

/* To avoid duplicated entry in the community-list, this function
   compares specified entry to existing entry.  */
static int community_list_dup_check(struct community_list *list, struct community_entry *new) {
	struct community_entry *entry;

	for(entry = list->head; entry; entry = entry->next) {
		if(entry->style != new->style) {
			continue;
		}

		if(entry->direct != new->direct) {
			continue;
		}

		if(entry->any != new->any) {
			continue;
		}

		if(entry->any) {
			return 1;
		}

		switch(entry->style) {
			case COMMUNITY_LIST_STANDARD:
				if(community_cmp(entry->u.com, new->u.com)) {
					return 1;
				}
				break;
			case EXTCOMMUNITY_LIST_STANDARD:
				if(ecommunity_cmp(entry->u.ecom, new->u.ecom)) {
					return 1;
				}
				break;
			case LARGE_COMMUNITY_LIST_STANDARD:
				if(lcommunity_cmp(entry->u.lcom, new->u.lcom)) {
					return 1;
				}
				break;
			case COMMUNITY_LIST_EXPANDED:
			case EXTCOMMUNITY_LIST_EXPANDED:
			case LARGE_COMMUNITY_LIST_EXPANDED:
				if(entry->config && new->config && strcmp(entry->config, new->config) == 0) {
					return 1;
				}
				if(!entry->config && !new->config) {
					return 1;
				}
				break;
			default: break;
		}
	}
	return 0;
}

/* Set community-list.  */
int community_list_set(struct community_list_handler *ch, const char *name, const char *str, int direct, int style) {
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct community *com = NULL;
	regex_t *regex = NULL;

	/* Get community list. */
	list = community_list_get(ch, name, COMMUNITY_LIST_MASTER);

	/* When community-list already has entry, new entry should have same
     style.  If you want to have mixed style community-list, you can
     comment out this check.  */
	if(!community_list_empty_p(list)) {
		struct community_entry *first;

		first = list->head;

		if(style != first->style) {
			return (first->style == COMMUNITY_LIST_STANDARD ? COMMUNITY_LIST_ERR_STANDARD_CONFLICT : COMMUNITY_LIST_ERR_EXPANDED_CONFLICT);
		}
	}

	if(str) {
		if(style == COMMUNITY_LIST_STANDARD) {
			com = community_str2com(str);
		} else {
			regex = bgp_regcomp(str);
		}

		if(!com && !regex) {
			return COMMUNITY_LIST_ERR_MALFORMED_VAL;
		}
	}

	entry = community_entry_new();
	entry->direct = direct;
	entry->style = style;
	entry->any = (str ? 0 : 1);
	entry->u.com = com;
	entry->reg = regex;
	entry->config = (regex ? XSTRDUP(MTYPE_COMMUNITY_LIST_CONFIG, str) : NULL);

	/* Do not put duplicated community entry.  */
	if(community_list_dup_check(list, entry)) {
		community_entry_free(entry);
	} else {
		community_list_entry_add(list, entry);
	}

	return 0;
}

/* Unset community-list.  When str is NULL, delete all of
   community-list entry belongs to the specified name.  */
int community_list_unset(struct community_list_handler *ch, const char *name, const char *str, int direct, int style) {
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct community *com = NULL;
	regex_t *regex = NULL;

	/* Lookup community list.  */
	list = community_list_lookup(ch, name, COMMUNITY_LIST_MASTER);
	if(list == NULL) {
		return COMMUNITY_LIST_ERR_CANT_FIND_LIST;
	}

	/* Delete all of entry belongs to this community-list.  */
	if(!str) {
		community_list_delete(list);
		return 0;
	}

	if(style == COMMUNITY_LIST_STANDARD) {
		com = community_str2com(str);
	} else {
		regex = bgp_regcomp(str);
	}

	if(!com && !regex) {
		return COMMUNITY_LIST_ERR_MALFORMED_VAL;
	}

	if(com) {
		entry = community_list_entry_lookup(list, com, direct);
	} else {
		entry = community_list_entry_lookup(list, str, direct);
	}

	if(com) {
		community_free(com);
	}
	if(regex) {
		bgp_regex_free(regex);
	}

	if(!entry) {
		return COMMUNITY_LIST_ERR_CANT_FIND_LIST;
	}

	community_list_entry_delete(list, entry, style);

	return 0;
}

/* Delete all permitted large communities in the list from com.  */
struct lcommunity *lcommunity_list_match_delete(struct lcommunity *lcom, struct community_list *list) {
	struct community_entry *entry;
	u_int32_t com_index_to_delete[lcom->size];
	u_char *ptr;
	int delete_index = 0;
	int i;

	/* Loop over each lcommunity value and evaluate each against the
   * community-list.  If we need to delete a community value add its index to
   * com_index_to_delete.
   */

	for(i = 0; i < lcom->size; i++) {
		ptr = lcom->val + (i * LCOMMUNITY_SIZE);
		for(entry = list->head; entry; entry = entry->next) {
			if(entry->any) {
				if(entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}

			else if((entry->style == LARGE_COMMUNITY_LIST_STANDARD) && lcommunity_include(entry->u.lcom, ptr)) {
				if(entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}

			else if((entry->style == LARGE_COMMUNITY_LIST_STANDARD) && entry->reg && lcommunity_regexp_include(entry->reg, lcom, i)) {
				if(entry->direct == COMMUNITY_PERMIT) {
					com_index_to_delete[delete_index] = i;
					delete_index++;
				}
				break;
			}
		}
	}

	/* Delete all of the communities we flagged for deletion */

	for(i = delete_index - 1; i >= 0; i--) {
		ptr = lcom->val + (com_index_to_delete[i] * LCOMMUNITY_SIZE);
		lcommunity_del_val(lcom, ptr);
	}

	return lcom;
}

/* Set lcommunity-list.  */
int lcommunity_list_set(struct community_list_handler *ch, const char *name, const char *str, int direct, int style) {
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct lcommunity *lcom = NULL;
	regex_t *regex = NULL;

	/* Get community list. */
	list = community_list_get(ch, name, LARGE_COMMUNITY_LIST_MASTER);

	/* When community-list already has entry, new entry should have same
     style.  If you want to have mixed style community-list, you can
     comment out this check.  */
	if(!community_list_empty_p(list)) {
		struct community_entry *first;

		first = list->head;

		if(style != first->style) {
			return (first->style == COMMUNITY_LIST_STANDARD ? COMMUNITY_LIST_ERR_STANDARD_CONFLICT : COMMUNITY_LIST_ERR_EXPANDED_CONFLICT);
		}
	}

	if(str) {
		if(style == LARGE_COMMUNITY_LIST_STANDARD) {
			lcom = lcommunity_str2com(str);
		} else {
			regex = bgp_regcomp(str);
		}

		if(!lcom && !regex) {
			return COMMUNITY_LIST_ERR_MALFORMED_VAL;
		}
	}

	entry = community_entry_new();
	entry->direct = direct;
	entry->style = style;
	entry->any = (str ? 0 : 1);
	entry->u.lcom = lcom;
	entry->reg = regex;
	if(lcom) {
		entry->config = lcommunity_lcom2str(lcom, LCOMMUNITY_FORMAT_COMMUNITY_LIST);
	} else if(regex) {
		entry->config = XSTRDUP(MTYPE_COMMUNITY_LIST_CONFIG, str);
	} else {
		entry->config = NULL;
	}

	/* Do not put duplicated community entry.  */
	if(community_list_dup_check(list, entry)) {
		community_entry_free(entry);
	} else {
		community_list_entry_add(list, entry);
	}

	return 0;
}

/* Unset community-list.  When str is NULL, delete all of
   community-list entry belongs to the specified name.  */
int lcommunity_list_unset(struct community_list_handler *ch, const char *name, const char *str, int direct, int style) {
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct lcommunity *lcom = NULL;
	regex_t *regex = NULL;

	/* Lookup community list.  */
	list = community_list_lookup(ch, name, LARGE_COMMUNITY_LIST_MASTER);
	if(list == NULL) {
		return COMMUNITY_LIST_ERR_CANT_FIND_LIST;
	}

	/* Delete all of entry belongs to this community-list.  */
	if(!str) {
		community_list_delete(list);
		return 0;
	}

	if(style == LARGE_COMMUNITY_LIST_STANDARD) {
		lcom = lcommunity_str2com(str);
	} else {
		regex = bgp_regcomp(str);
	}

	if(!lcom && !regex) {
		return COMMUNITY_LIST_ERR_MALFORMED_VAL;
	}

	if(lcom) {
		entry = community_list_entry_lookup(list, lcom, direct);
	} else {
		entry = community_list_entry_lookup(list, str, direct);
	}

	if(lcom) {
		lcommunity_free(&lcom);
	}
	if(regex) {
		bgp_regex_free(regex);
	}

	if(!entry) {
		return COMMUNITY_LIST_ERR_CANT_FIND_LIST;
	}

	community_list_entry_delete(list, entry, style);

	return 0;
}

/* Set extcommunity-list.  */
int extcommunity_list_set(struct community_list_handler *ch, const char *name, const char *str, int direct, int style) {
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct ecommunity *ecom = NULL;
	regex_t *regex = NULL;

	entry = NULL;

	/* Get community list. */
	list = community_list_get(ch, name, EXTCOMMUNITY_LIST_MASTER);

	/* When community-list already has entry, new entry should have same
     style.  If you want to have mixed style community-list, you can
     comment out this check.  */
	if(!community_list_empty_p(list)) {
		struct community_entry *first;

		first = list->head;

		if(style != first->style) {
			return (first->style == EXTCOMMUNITY_LIST_STANDARD ? COMMUNITY_LIST_ERR_STANDARD_CONFLICT : COMMUNITY_LIST_ERR_EXPANDED_CONFLICT);
		}
	}

	if(str) {
		if(style == EXTCOMMUNITY_LIST_STANDARD) {
			ecom = ecommunity_str2com(str, 0, 1);
		} else {
			regex = bgp_regcomp(str);
		}

		if(!ecom && !regex) {
			return COMMUNITY_LIST_ERR_MALFORMED_VAL;
		}
	}

	if(ecom) {
		ecom->str = ecommunity_ecom2str(ecom, ECOMMUNITY_FORMAT_DISPLAY);
	}

	entry = community_entry_new();
	entry->direct = direct;
	entry->style = style;
	entry->any = (str ? 0 : 1);
	if(ecom) {
		entry->config = ecommunity_ecom2str(ecom, ECOMMUNITY_FORMAT_COMMUNITY_LIST);
	} else if(regex) {
		entry->config = XSTRDUP(MTYPE_COMMUNITY_LIST_CONFIG, str);
	} else {
		entry->config = NULL;
	}
	entry->u.ecom = ecom;
	entry->reg = regex;

	/* Do not put duplicated community entry.  */
	if(community_list_dup_check(list, entry)) {
		community_entry_free(entry);
	} else {
		community_list_entry_add(list, entry);
	}

	return 0;
}

/* Unset extcommunity-list.  When str is NULL, delete all of
   extcommunity-list entry belongs to the specified name.  */
int extcommunity_list_unset(struct community_list_handler *ch, const char *name, const char *str, int direct, int style) {
	struct community_entry *entry = NULL;
	struct community_list *list;
	struct ecommunity *ecom = NULL;
	regex_t *regex = NULL;

	/* Lookup extcommunity list.  */
	list = community_list_lookup(ch, name, EXTCOMMUNITY_LIST_MASTER);
	if(list == NULL) {
		return COMMUNITY_LIST_ERR_CANT_FIND_LIST;
	}

	/* Delete all of entry belongs to this extcommunity-list.  */
	if(!str) {
		community_list_delete(list);
		return 0;
	}

	if(style == EXTCOMMUNITY_LIST_STANDARD) {
		ecom = ecommunity_str2com(str, 0, 1);
	} else {
		regex = bgp_regcomp(str);
	}

	if(!ecom && !regex) {
		return COMMUNITY_LIST_ERR_MALFORMED_VAL;
	}

	if(ecom) {
		entry = community_list_entry_lookup(list, ecom, direct);
	} else {
		entry = community_list_entry_lookup(list, str, direct);
	}

	if(ecom) {
		ecommunity_free(&ecom);
	}
	if(regex) {
		bgp_regex_free(regex);
	}

	if(!entry) {
		return COMMUNITY_LIST_ERR_CANT_FIND_LIST;
	}

	community_list_entry_delete(list, entry, style);

	return 0;
}

/* Initializa community-list.  Return community-list handler.  */
struct community_list_handler *community_list_init(void) {
	struct community_list_handler *ch;
	ch = XCALLOC(MTYPE_COMMUNITY_LIST_HANDLER, sizeof(struct community_list_handler));
	return ch;
}

/* Terminate community-list.  */
void community_list_terminate(struct community_list_handler *ch) {
	struct community_list_master *cm;
	struct community_list *list;

	cm = &ch->community_list;
	while((list = cm->num.head) != NULL) {
		community_list_delete(list);
	}
	while((list = cm->str.head) != NULL) {
		community_list_delete(list);
	}

	cm = &ch->lcommunity_list;
	while((list = cm->num.head) != NULL) {
		community_list_delete(list);
	}
	while((list = cm->str.head) != NULL) {
		community_list_delete(list);
	}

	cm = &ch->extcommunity_list;
	while((list = cm->num.head) != NULL) {
		community_list_delete(list);
	}
	while((list = cm->str.head) != NULL) {
		community_list_delete(list);
	}

	XFREE(MTYPE_COMMUNITY_LIST_HANDLER, ch);
}
