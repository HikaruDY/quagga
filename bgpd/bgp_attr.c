/* BGP attributes management routines.
   Copyright (C) 1996, 97, 98, 1999 Kunihiro Ishiguro
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

#include "linklist.h"
#include "prefix.h"
#include "memory.h"
#include "vector.h"
#include "vty.h"
#include "stream.h"
#include "log.h"
#include "hash.h"
#include "jhash.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "table.h"
#include "bgp_encap_types.h"

/* Attribute strings for logging. */
static const struct message attr_str[] = {
	{BGP_ATTR_ORIGIN,		   "ORIGIN"	    },
	{ BGP_ATTR_AS_PATH,	    "AS_PATH"	      },
	{ BGP_ATTR_NEXT_HOP,	     "NEXT_HOP"	},
	{ BGP_ATTR_MULTI_EXIT_DISC,   "MULTI_EXIT_DISC" },
	{ BGP_ATTR_LOCAL_PREF,	       "LOCAL_PREF"	    },
	{ BGP_ATTR_ATOMIC_AGGREGATE,  "ATOMIC_AGGREGATE"},
	{ BGP_ATTR_AGGREGATOR,	       "AGGREGATOR"	    },
	{ BGP_ATTR_COMMUNITIES,	"COMMUNITY"	    },
	{ BGP_ATTR_ORIGINATOR_ID,	  "ORIGINATOR_ID"	  },
	{ BGP_ATTR_CLUSTER_LIST,	 "CLUSTER_LIST"	},
	{ BGP_ATTR_DPA,		"DPA"	      },
	{ BGP_ATTR_ADVERTISER,	       "ADVERTISER"	    },
	{ BGP_ATTR_RCID_PATH,	      "RCID_PATH"	  },
	{ BGP_ATTR_MP_REACH_NLRI,	  "MP_REACH_NLRI"	  },
	{ BGP_ATTR_MP_UNREACH_NLRI,   "MP_UNREACH_NLRI" },
	{ BGP_ATTR_EXT_COMMUNITIES,   "EXT_COMMUNITIES" },
	{ BGP_ATTR_AS4_PATH,	     "AS4_PATH"	},
	{ BGP_ATTR_AS4_AGGREGATOR,	   "AS4_AGGREGATOR"  },
	{ BGP_ATTR_AS_PATHLIMIT,	 "AS_PATHLIMIT"	},
	{ BGP_ATTR_ENCAP,		  "ENCAP"		  },
	{ 21,			 ""		 },
	{ 22,			 ""		 },
	{ 23,			 ""		 },
	{ 24,			 ""		 },
	{ 25,			 ""		 },
	{ 26,			 ""		 },
	{ 27,			 ""		 },
	{ 28,			 ""		 },
	{ 29,			 ""		 },
	{ 30,			 ""		 },
	{ 31,			 ""		 },
	{ BGP_ATTR_LARGE_COMMUNITIES, "LARGE_COMMUNITY" }
};
static const int attr_str_max = array_size(attr_str);

static const struct message attr_flag_str[] = {
	{BGP_ATTR_FLAG_OPTIONAL, "Optional"	     },
	{ BGP_ATTR_FLAG_TRANS,   "Transitive"	    },
	{ BGP_ATTR_FLAG_PARTIAL, "Partial"	   },
 /* bgp_attr_flags_diagnose() relies on this bit being last in this list */
	{ BGP_ATTR_FLAG_EXTLEN,	"Extended Length"},
};

static struct hash *cluster_hash;

static void *cluster_hash_alloc(void *p) {
	struct cluster_list *val = (struct cluster_list *) p;
	struct cluster_list *cluster;

	cluster = XMALLOC(MTYPE_CLUSTER, sizeof(struct cluster_list));
	cluster->length = val->length;

	if(cluster->length) {
		cluster->list = XMALLOC(MTYPE_CLUSTER_VAL, val->length);
		memcpy(cluster->list, val->list, val->length);
	} else {
		cluster->list = NULL;
	}

	cluster->refcnt = 0;

	return cluster;
}

/* Cluster list related functions. */
static struct cluster_list *cluster_parse(struct in_addr *pnt, int length) {
	struct cluster_list tmp;
	struct cluster_list *cluster;

	tmp.length = length;
	tmp.list = pnt;

	cluster = hash_get(cluster_hash, &tmp, cluster_hash_alloc);
	cluster->refcnt++;
	return cluster;
}

int cluster_loop_check(struct cluster_list *cluster, struct in_addr originator) {
	int i;

	for(i = 0; i < cluster->length / 4; i++) {
		if(cluster->list[i].s_addr == originator.s_addr) {
			return 1;
		}
	}
	return 0;
}

static unsigned int cluster_hash_key_make(void *p) {
	const struct cluster_list *cluster = p;

	return jhash(cluster->list, cluster->length, 0);
}

static int cluster_hash_cmp(const void *p1, const void *p2) {
	const struct cluster_list *cluster1 = p1;
	const struct cluster_list *cluster2 = p2;

	return (cluster1->length == cluster2->length && memcmp(cluster1->list, cluster2->list, cluster1->length) == 0);
}

static void cluster_free(struct cluster_list *cluster) {
	if(cluster->list) {
		XFREE(MTYPE_CLUSTER_VAL, cluster->list);
	}
	XFREE(MTYPE_CLUSTER, cluster);
}

#if 0
static struct cluster_list *
cluster_dup (struct cluster_list *cluster)
{
  struct cluster_list *new;

  new = XCALLOC (MTYPE_CLUSTER, sizeof (struct cluster_list));
  new->length = cluster->length;

  if (cluster->length)
    {
      new->list = XMALLOC (MTYPE_CLUSTER_VAL, cluster->length);
      memcpy (new->list, cluster->list, cluster->length);
    }
  else
    new->list = NULL;

  return new;
}
#endif

static struct cluster_list *cluster_intern(struct cluster_list *cluster) {
	struct cluster_list *find;

	find = hash_get(cluster_hash, cluster, cluster_hash_alloc);
	find->refcnt++;

	return find;
}

void cluster_unintern(struct cluster_list **cluster) {
	struct cluster_list *c = *cluster;
	if(c->refcnt) {
		c->refcnt--;
	}

	if(c->refcnt == 0) {
		hash_release(cluster_hash, c);
		cluster_free(c);
		*cluster = NULL;
	}
}

static void cluster_init(void) {
	cluster_hash = hash_create(cluster_hash_key_make, cluster_hash_cmp);
}

static void cluster_finish(void) {
	hash_clean(cluster_hash, (void (*)(void *)) cluster_free);
	hash_free(cluster_hash);
	cluster_hash = NULL;
}

struct bgp_attr_encap_subtlv *encap_tlv_dup(struct bgp_attr_encap_subtlv *orig) {
	struct bgp_attr_encap_subtlv *new;
	struct bgp_attr_encap_subtlv *tail;
	struct bgp_attr_encap_subtlv *p;

	for(p = orig, tail = new = NULL; p; p = p->next) {
		int size = sizeof(struct bgp_attr_encap_subtlv) - 1 + p->length;
		if(tail) {
			tail->next = XCALLOC(MTYPE_ENCAP_TLV, size);
			tail = tail->next;
		} else {
			tail = new = XCALLOC(MTYPE_ENCAP_TLV, size);
		}
		assert(tail);
		memcpy(tail, p, size);
		tail->next = NULL;
	}

	return new;
}

static void encap_free(struct bgp_attr_encap_subtlv *p) {
	struct bgp_attr_encap_subtlv *next;
	while(p) {
		next = p->next;
		p->next = NULL;
		XFREE(MTYPE_ENCAP_TLV, p);
		p = next;
	}
}

void bgp_attr_flush_encap(struct attr *attr) {
	if(!attr || !attr->extra) {
		return;
	}

	if(attr->extra->encap_subtlvs) {
		encap_free(attr->extra->encap_subtlvs);
		attr->extra->encap_subtlvs = NULL;
	}
}

/*
 * Compare encap sub-tlv chains
 *
 *	1 = equivalent
 *	0 = not equivalent
 *
 * This algorithm could be made faster if needed
 */
static int encap_same(struct bgp_attr_encap_subtlv *h1, struct bgp_attr_encap_subtlv *h2) {
	struct bgp_attr_encap_subtlv *p;
	struct bgp_attr_encap_subtlv *q;

	if(!h1 && !h2) {
		return 1;
	}
	if(h1 && !h2) {
		return 0;
	}
	if(!h1 && h2) {
		return 0;
	}
	if(h1 == h2) {
		return 1;
	}

	for(p = h1; p; p = p->next) {
		for(q = h2; q; q = q->next) {
			if((p->type == q->type) && (p->length == q->length) && !memcmp(p->value, q->value, p->length)) {
				break;
			}
		}
		if(!q) {
			return 0;
		}
	}

	for(p = h2; p; p = p->next) {
		for(q = h1; q; q = q->next) {
			if((p->type == q->type) && (p->length == q->length) && !memcmp(p->value, q->value, p->length)) {
				break;
			}
		}
		if(!q) {
			return 0;
		}
	}

	return 1;
}

/* Unknown transit attribute. */
static struct hash *transit_hash;

static void transit_free(struct transit *transit) {
	if(transit->val) {
		XFREE(MTYPE_TRANSIT_VAL, transit->val);
	}
	XFREE(MTYPE_TRANSIT, transit);
}

static void *transit_hash_alloc(void *p) {
	/* Transit structure is already allocated.  */
	return p;
}

static struct transit *transit_intern(struct transit *transit) {
	struct transit *find;

	find = hash_get(transit_hash, transit, transit_hash_alloc);
	if(find != transit) {
		transit_free(transit);
	}
	find->refcnt++;

	return find;
}

void transit_unintern(struct transit **transit) {
	struct transit *t = *transit;

	if(t->refcnt) {
		t->refcnt--;
	}

	if(t->refcnt == 0) {
		hash_release(transit_hash, t);
		transit_free(t);
		*transit = NULL;
	}
}

static unsigned int transit_hash_key_make(void *p) {
	const struct transit *transit = p;

	return jhash(transit->val, transit->length, 0);
}

static int transit_hash_cmp(const void *p1, const void *p2) {
	const struct transit *transit1 = p1;
	const struct transit *transit2 = p2;

	return (transit1->length == transit2->length && memcmp(transit1->val, transit2->val, transit1->length) == 0);
}

static void transit_init(void) {
	transit_hash = hash_create(transit_hash_key_make, transit_hash_cmp);
}

static void transit_finish(void) {
	hash_clean(transit_hash, (void (*)(void *)) transit_free);
	hash_free(transit_hash);
	transit_hash = NULL;
}

/* Attribute hash routines. */
static struct hash *attrhash;

static struct attr_extra *bgp_attr_extra_new(void) {
	return XCALLOC(MTYPE_ATTR_EXTRA, sizeof(struct attr_extra));
}

void bgp_attr_extra_free(struct attr *attr) {
	if(attr->extra) {
		if(attr->extra->encap_subtlvs) {
			encap_free(attr->extra->encap_subtlvs);
			attr->extra->encap_subtlvs = NULL;
		}
		XFREE(MTYPE_ATTR_EXTRA, attr->extra);
		attr->extra = NULL;
	}
}

struct attr_extra *bgp_attr_extra_get(struct attr *attr) {
	if(!attr->extra) {
		attr->extra = bgp_attr_extra_new();
	}
	return attr->extra;
}

/* Shallow copy of an attribute
 * Though, not so shallow that it doesn't copy the contents
 * of the attr_extra pointed to by 'extra'
 */
void bgp_attr_dup(struct attr *new, struct attr *orig) {
	struct attr_extra *extra = new->extra;

	*new = *orig;
	/* if caller provided attr_extra space, use it in any case.
   *
   * This is neccesary even if orig->extra equals NULL, because otherwise
   * memory may be later allocated on the heap by bgp_attr_extra_get.
   *
   * That memory would eventually be leaked, because the caller must not
   * call bgp_attr_extra_free if he provided attr_extra on the stack.
   */
	if(extra) {
		new->extra = extra;
		memset(new->extra, 0, sizeof(struct attr_extra));
		if(orig->extra) {
			*new->extra = *orig->extra;
			if(orig->extra->encap_subtlvs) {
				new->extra->encap_subtlvs = encap_tlv_dup(orig->extra->encap_subtlvs);
			}
		}
	} else if(orig->extra) {
		new->extra = bgp_attr_extra_new();
		*new->extra = *orig->extra;
		if(orig->extra->encap_subtlvs) {
			new->extra->encap_subtlvs = encap_tlv_dup(orig->extra->encap_subtlvs);
		}
	}
}

unsigned long int attr_count(void) {
	return attrhash->count;
}

unsigned long int attr_unknown_count(void) {
	return transit_hash->count;
}

unsigned int attrhash_key_make(void *p) {
	const struct attr *attr = (struct attr *) p;
	const struct attr_extra *extra = attr->extra;
	uint32_t key = 0;
#define MIX(val) key = jhash_1word(val, key)

	MIX(attr->origin);
	MIX(attr->nexthop.s_addr);
	MIX(attr->med);
	MIX(attr->local_pref);

	key += attr->origin;
	key += attr->nexthop.s_addr;
	key += attr->med;
	key += attr->local_pref;

	if(extra) {
		MIX(extra->aggregator_as);
		MIX(extra->aggregator_addr.s_addr);
		MIX(extra->weight);
		MIX(extra->priority);
		MIX(extra->mp_nexthop_global_in.s_addr);
		MIX(extra->originator_id.s_addr);
		MIX(extra->tag);
	}

	if(attr->aspath) {
		MIX(aspath_key_make(attr->aspath));
	}
	if(attr->community) {
		MIX(community_hash_make(attr->community));
	}

	if(extra) {
		if(extra->lcommunity) {
			MIX(lcommunity_hash_make(extra->lcommunity));
		}
		if(extra->ecommunity) {
			MIX(ecommunity_hash_make(extra->ecommunity));
		}
		if(extra->cluster) {
			MIX(cluster_hash_key_make(extra->cluster));
		}
		if(extra->transit) {
			MIX(transit_hash_key_make(extra->transit));
		}

		MIX(extra->mp_nexthop_len);
		key = jhash(extra->mp_nexthop_global.s6_addr, 16, key);
		key = jhash(extra->mp_nexthop_local.s6_addr, 16, key);
	}

	return key;
}

int attrhash_cmp(const void *p1, const void *p2) {
	const struct attr *attr1 = p1;
	const struct attr *attr2 = p2;

	if(attr1->flag == attr2->flag && attr1->origin == attr2->origin && attr1->nexthop.s_addr == attr2->nexthop.s_addr && attr1->aspath == attr2->aspath && attr1->community == attr2->community && attr1->med == attr2->med
	   && attr1->local_pref == attr2->local_pref) {
		const struct attr_extra *ae1 = attr1->extra;
		const struct attr_extra *ae2 = attr2->extra;

		if(ae1 && ae2 && ae1->aggregator_as == ae2->aggregator_as && ae1->aggregator_addr.s_addr == ae2->aggregator_addr.s_addr && ae1->weight == ae2->weight && ae1->priority == ae2->priority && ae1->tag == ae2->tag && ae1->mp_nexthop_len == ae2->mp_nexthop_len
		   && IPV6_ADDR_SAME(&ae1->mp_nexthop_global, &ae2->mp_nexthop_global) && IPV6_ADDR_SAME(&ae1->mp_nexthop_local, &ae2->mp_nexthop_local) && IPV4_ADDR_SAME(&ae1->mp_nexthop_global_in, &ae2->mp_nexthop_global_in)
		   && ae1->ecommunity == ae2->ecommunity && ae1->lcommunity == ae2->lcommunity && ae1->cluster == ae2->cluster && ae1->transit == ae2->transit && (ae1->encap_tunneltype == ae2->encap_tunneltype)
		   && encap_same(ae1->encap_subtlvs, ae2->encap_subtlvs) && IPV4_ADDR_SAME(&ae1->originator_id, &ae2->originator_id)) {
			return 1;
		} else if(ae1 || ae2) {
			return 0;
		}
		/* neither attribute has extra attributes, so they're same */
		return 1;
	} else {
		return 0;
	}
}

static void attrhash_init(void) {
	attrhash = hash_create(attrhash_key_make, attrhash_cmp);
}

/*
 * special for hash_clean below
 */
static void attr_vfree(void *attr) {
	bgp_attr_extra_free((struct attr *) attr);
	XFREE(MTYPE_ATTR, attr);
}

static void attrhash_finish(void) {
	hash_clean(attrhash, attr_vfree);
	hash_free(attrhash);
	attrhash = NULL;
}

static void attr_show_all_iterator(struct hash_backet *backet, struct vty *vty) {
	struct attr *attr = backet->data;

	vty_out(vty, "attr[%ld] nexthop %s%s", attr->refcnt, inet_ntoa(attr->nexthop), VTY_NEWLINE);
}

void attr_show_all(struct vty *vty) {
	hash_iterate(attrhash, (void (*)(struct hash_backet *, void *)) attr_show_all_iterator, vty);
}

static void *bgp_attr_hash_alloc(void *p) {
	struct attr *val = (struct attr *) p;
	struct attr *attr;

	attr = XMALLOC(MTYPE_ATTR, sizeof(struct attr));
	*attr = *val;
	if(val->extra) {
		attr->extra = bgp_attr_extra_new();
		*attr->extra = *val->extra;

		if(attr->extra->encap_subtlvs) {
			attr->extra->encap_subtlvs = encap_tlv_dup(attr->extra->encap_subtlvs);
		}
	}
	attr->refcnt = 0;
	return attr;
}

/* Internet argument attribute. */
struct attr *bgp_attr_intern(struct attr *attr) {
	struct attr *find;

	/* Intern referenced strucutre. */
	if(attr->aspath) {
		if(!attr->aspath->refcnt) {
			attr->aspath = aspath_intern(attr->aspath);
		} else {
			attr->aspath->refcnt++;
		}
	}
	if(attr->community) {
		if(!attr->community->refcnt) {
			attr->community = community_intern(attr->community);
		} else {
			attr->community->refcnt++;
		}
	}
	if(attr->extra) {
		struct attr_extra *attre = attr->extra;

		if(attre->ecommunity) {
			if(!attre->ecommunity->refcnt) {
				attre->ecommunity = ecommunity_intern(attre->ecommunity);
			} else {
				attre->ecommunity->refcnt++;
			}
		}
		if(attre->lcommunity) {
			if(!attre->lcommunity->refcnt) {
				attre->lcommunity = lcommunity_intern(attre->lcommunity);
			} else {
				attre->lcommunity->refcnt++;
			}
		}
		if(attre->cluster) {
			if(!attre->cluster->refcnt) {
				attre->cluster = cluster_intern(attre->cluster);
			} else {
				attre->cluster->refcnt++;
			}
		}
		if(attre->transit) {
			if(!attre->transit->refcnt) {
				attre->transit = transit_intern(attre->transit);
			} else {
				attre->transit->refcnt++;
			}
		}
	}

	find = (struct attr *) hash_get(attrhash, attr, bgp_attr_hash_alloc);
	find->refcnt++;

	return find;
}

/* Make network statement's attribute. */
struct attr *bgp_attr_default_set(struct attr *attr, u_char origin) {
	memset(attr, 0, sizeof(struct attr));
	bgp_attr_extra_get(attr);

	attr->origin = origin;
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_ORIGIN);
	attr->aspath = aspath_empty();
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_AS_PATH);
	attr->extra->weight = BGP_ATTR_DEFAULT_WEIGHT;
	attr->extra->priority = BGP_ATTR_DEFAULT_PRIORITY;
	attr->extra->tag = 0;
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP);
	attr->extra->mp_nexthop_len = IPV6_MAX_BYTELEN;

	return attr;
}

/* Make network statement's attribute. */
struct attr *bgp_attr_default_intern(u_char origin) {
	struct attr attr;
	struct attr *new;

	memset(&attr, 0, sizeof(struct attr));
	bgp_attr_extra_get(&attr);

	bgp_attr_default_set(&attr, origin);

	new = bgp_attr_intern(&attr);
	bgp_attr_extra_free(&attr);

	aspath_unintern(&new->aspath);
	return new;
}

/* Create the attributes for an aggregate */
struct attr *bgp_attr_aggregate_intern(struct bgp *bgp, u_char origin, struct aspath *aspath, struct community *community, int as_set, u_char atomic_aggregate) {
	struct attr attr;
	struct attr *new;
	struct attr_extra attre;

	memset(&attr, 0, sizeof(struct attr));
	memset(&attre, 0, sizeof(struct attr_extra));
	attr.extra = &attre;

	/* Origin attribute. */
	attr.origin = origin;
	attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_ORIGIN);

	/* AS path attribute. */
	if(aspath) {
		attr.aspath = aspath_intern(aspath);
	} else {
		attr.aspath = aspath_empty();
	}
	attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_AS_PATH);

	/* Next hop attribute.  */
	attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP);

	if(community) {
		attr.community = community;
		attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_COMMUNITIES);
	}

	attre.weight = BGP_ATTR_DEFAULT_WEIGHT;
	attre.priority = bgp->default_priority;
	attre.mp_nexthop_len = IPV6_MAX_BYTELEN;

	if(!as_set || atomic_aggregate) {
		attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_ATOMIC_AGGREGATE);
	}
	attr.flag |= ATTR_FLAG_BIT(BGP_ATTR_AGGREGATOR);
	if(CHECK_FLAG(bgp->config, BGP_CONFIG_CONFEDERATION)) {
		attre.aggregator_as = bgp->confed_id;
	} else {
		attre.aggregator_as = bgp->as;
	}
	attre.aggregator_addr = bgp->router_id;

	new = bgp_attr_intern(&attr);

	aspath_unintern(&new->aspath);
	return new;
}

/* Unintern just the sub-components of the attr, but not the attr */
void bgp_attr_unintern_sub(struct attr *attr) {
	/* aspath refcount shoud be decrement. */
	if(attr->aspath) {
		aspath_unintern(&attr->aspath);
	}
	UNSET_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_AS_PATH));

	if(attr->community) {
		community_unintern(&attr->community);
	}
	UNSET_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_COMMUNITIES));

	if(attr->extra) {
		if(attr->extra->ecommunity) {
			ecommunity_unintern(&attr->extra->ecommunity);
		}
		UNSET_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_EXT_COMMUNITIES));

		if(attr->extra->lcommunity) {
			lcommunity_unintern(&attr->extra->lcommunity);
		}
		UNSET_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_LARGE_COMMUNITIES));

		if(attr->extra->cluster) {
			cluster_unintern(&attr->extra->cluster);
		}
		UNSET_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_CLUSTER_LIST));

		if(attr->extra->transit) {
			transit_unintern(&attr->extra->transit);
		}
	}
}

/* Free bgp attribute and aspath. */
void bgp_attr_unintern(struct attr **pattr) {
	struct attr *attr = *pattr;
	struct attr *ret;
	struct attr tmp;
	struct attr_extra tmp_extra;

	/* Decrement attribute reference. */
	attr->refcnt--;

	tmp = *attr;

	if(attr->extra) {
		tmp.extra = &tmp_extra;
		memcpy(tmp.extra, attr->extra, sizeof(struct attr_extra));
	}

	/* If reference becomes zero then free attribute object. */
	if(attr->refcnt == 0) {
		ret = hash_release(attrhash, attr);
		assert(ret != NULL);
		bgp_attr_extra_free(attr);
		XFREE(MTYPE_ATTR, attr);
		*pattr = NULL;
	}

	bgp_attr_unintern_sub(&tmp);
}

void bgp_attr_flush(struct attr *attr) {
	if(attr->aspath && !attr->aspath->refcnt) {
		aspath_free(attr->aspath);
		attr->aspath = NULL;
	}
	if(attr->community && !attr->community->refcnt) {
		community_free(attr->community);
		attr->community = NULL;
	}
	if(attr->extra) {
		struct attr_extra *attre = attr->extra;

		if(attre->ecommunity && !attre->ecommunity->refcnt) {
			ecommunity_free(&attre->ecommunity);
		}
		if(attre->lcommunity && !attre->lcommunity->refcnt) {
			lcommunity_free(&attre->lcommunity);
		}
		if(attre->cluster && !attre->cluster->refcnt) {
			cluster_free(attre->cluster);
			attre->cluster = NULL;
		}
		if(attre->transit && !attre->transit->refcnt) {
			transit_free(attre->transit);
			attre->transit = NULL;
		}
		encap_free(attre->encap_subtlvs);
		attre->encap_subtlvs = NULL;
	}
}

/* Implement some draft-ietf-idr-error-handling behaviour and
 * avoid resetting sessions for malformed attributes which are
 * are partial/optional and hence where the error likely was not
 * introduced by the sending neighbour.
 */
static bgp_attr_parse_ret_t bgp_attr_malformed(struct bgp_attr_parser_args *args, u_char subcode, bgp_size_t length) {
	struct peer *const peer = args->peer;
	const u_int8_t flags = args->flags;
	/* startp and length must be special-cased, as whether or not to
   * send the attribute data with the NOTIFY depends on the error,
   * the caller therefore signals this with the seperate length argument
   */
	u_char *notify_datap = (length > 0 ? args->startp : NULL);

	/* The malformed attribute shouldn't be passed on, should
   * we decide to proceed with parsing the UPDATE
   */
	UNSET_FLAG(args->attr->flag, ATTR_FLAG_BIT(args->type));

	/* Only relax error handling for eBGP peers */
	if(peer->sort != BGP_PEER_EBGP) {
		bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, subcode, notify_datap, length);
		return BGP_ATTR_PARSE_ERROR;
	}

	/* Adjust the stream getp to the end of the attribute, in case we can
   * still proceed but the caller hasn't read all the attribute.
   */
	stream_set_getp(BGP_INPUT(peer), (args->startp - STREAM_DATA(BGP_INPUT(peer))) + args->total);

	switch(args->type) {
		/* where an attribute is relatively inconsequential, e.g. it does not
     * affect route selection, and can be safely ignored, then any such
     * attributes which are malformed should just be ignored and the route
     * processed as normal.
     */
		case BGP_ATTR_AS4_AGGREGATOR:
		case BGP_ATTR_AGGREGATOR:
		case BGP_ATTR_ATOMIC_AGGREGATE: return BGP_ATTR_PARSE_PROCEED;

		/* Core attributes, particularly ones which may influence route
     * selection, should always cause session resets
     */
		case BGP_ATTR_ORIGIN:
		case BGP_ATTR_AS_PATH:
		case BGP_ATTR_NEXT_HOP:
		case BGP_ATTR_MULTI_EXIT_DISC:
		case BGP_ATTR_LOCAL_PREF:
		case BGP_ATTR_COMMUNITIES:
		case BGP_ATTR_ORIGINATOR_ID:
		case BGP_ATTR_CLUSTER_LIST:
		case BGP_ATTR_MP_REACH_NLRI:
		case BGP_ATTR_MP_UNREACH_NLRI:
		case BGP_ATTR_EXT_COMMUNITIES: bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, subcode, notify_datap, length); return BGP_ATTR_PARSE_ERROR;
	}

	/* Partial optional attributes that are malformed should not cause
   * the whole session to be reset. Instead treat it as a withdrawal
   * of the routes, if possible.
   */
	if(CHECK_FLAG(flags, BGP_ATTR_FLAG_TRANS) && CHECK_FLAG(flags, BGP_ATTR_FLAG_OPTIONAL) && CHECK_FLAG(flags, BGP_ATTR_FLAG_PARTIAL)) {
		return BGP_ATTR_PARSE_WITHDRAW;
	}

	/* default to reset */
	bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, subcode, notify_datap, length);
	return BGP_ATTR_PARSE_ERROR;
}

/* Find out what is wrong with the path attribute flag bits and log the error.
   "Flag bits" here stand for Optional, Transitive and Partial, but not for
   Extended Length. Checking O/T/P bits at once implies, that the attribute
   being diagnosed is defined by RFC as either a "well-known" or an "optional,
   non-transitive" attribute. */
static void bgp_attr_flags_diagnose(
	struct bgp_attr_parser_args *args, u_int8_t desired_flags /* how RFC says it must be */
) {
	u_char seen = 0, i;
	u_char real_flags = args->flags;
	const u_int8_t attr_code = args->type;

	desired_flags &= ~BGP_ATTR_FLAG_EXTLEN;
	real_flags &= ~BGP_ATTR_FLAG_EXTLEN;
	for(i = 0; i <= 2; i++) { /* O,T,P, but not E */
		if(CHECK_FLAG(desired_flags, attr_flag_str[i].key) != CHECK_FLAG(real_flags, attr_flag_str[i].key)) {
			zlog(args->peer->log, LOG_ERR, "%s attribute must%s be flagged as \"%s\"", LOOKUP(attr_str, attr_code), CHECK_FLAG(desired_flags, attr_flag_str[i].key) ? "" : " not", attr_flag_str[i].str);
			seen = 1;
		}
	}
	if(!seen) {
		zlog(args->peer->log, LOG_DEBUG,
		     "Strange, %s called for attr %s, but no problem found with flags"
		     " (real flags 0x%x, desired 0x%x)",
		     __func__, LOOKUP(attr_str, attr_code), real_flags, desired_flags);
	}
}

/* Required flags for attributes. EXTLEN will be masked off when testing,
 * as will PARTIAL for optional+transitive attributes.
 */
const u_int8_t attr_flags_values[] = { [BGP_ATTR_ORIGIN] = BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_AS_PATH] = BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_NEXT_HOP] = BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_MULTI_EXIT_DISC] = BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_LOCAL_PREF] = BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_ATOMIC_AGGREGATE] = BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_AGGREGATOR] = BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_COMMUNITIES] = BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_ORIGINATOR_ID] = BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_CLUSTER_LIST] = BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_MP_REACH_NLRI] = BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_MP_UNREACH_NLRI] = BGP_ATTR_FLAG_OPTIONAL,
				       [BGP_ATTR_EXT_COMMUNITIES] = BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_AS4_PATH] = BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_AS4_AGGREGATOR] = BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS,
				       [BGP_ATTR_LARGE_COMMUNITIES] = BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_OPTIONAL };
static const size_t attr_flags_values_max = array_size(attr_flags_values) - 1;

static int bgp_attr_flag_invalid(struct bgp_attr_parser_args *args) {
	u_int8_t mask = BGP_ATTR_FLAG_EXTLEN;
	const u_int8_t flags = args->flags;
	const u_int8_t attr_code = args->type;
	struct peer *const peer = args->peer;

	/* there may be attributes we don't know about */
	if(attr_code > attr_flags_values_max) {
		return 0;
	}
	if(attr_flags_values[attr_code] == 0) {
		return 0;
	}

	/* RFC4271, "For well-known attributes, the Transitive bit MUST be set to
   * 1."
   */
	if(!CHECK_FLAG(BGP_ATTR_FLAG_OPTIONAL, flags) && !CHECK_FLAG(BGP_ATTR_FLAG_TRANS, flags)) {
		zlog(peer->log, LOG_ERR, "%s well-known attributes must have transitive flag set (%x)", LOOKUP(attr_str, attr_code), flags);
		return 1;
	}

	/* "For well-known attributes and for optional non-transitive attributes,
   *  the Partial bit MUST be set to 0."
   */
	if(CHECK_FLAG(flags, BGP_ATTR_FLAG_PARTIAL)) {
		if(!CHECK_FLAG(flags, BGP_ATTR_FLAG_OPTIONAL)) {
			zlog(peer->log, LOG_ERR,
			     "%s well-known attribute "
			     "must NOT have the partial flag set (%x)",
			     LOOKUP(attr_str, attr_code), flags);
			return 1;
		}
		if(CHECK_FLAG(flags, BGP_ATTR_FLAG_OPTIONAL) && !CHECK_FLAG(flags, BGP_ATTR_FLAG_TRANS)) {
			zlog(peer->log, LOG_ERR,
			     "%s optional + transitive attribute "
			     "must NOT have the partial flag set (%x)",
			     LOOKUP(attr_str, attr_code), flags);
			return 1;
		}
	}

	/* Optional transitive attributes may go through speakers that don't
   * reocgnise them and set the Partial bit.
   */
	if(CHECK_FLAG(flags, BGP_ATTR_FLAG_OPTIONAL) && CHECK_FLAG(flags, BGP_ATTR_FLAG_TRANS)) {
		SET_FLAG(mask, BGP_ATTR_FLAG_PARTIAL);
	}

	if((flags & ~mask) == attr_flags_values[attr_code]) {
		return 0;
	}

	bgp_attr_flags_diagnose(args, attr_flags_values[attr_code]);
	return 1;
}

/* Get origin attribute of the update message. */
static bgp_attr_parse_ret_t bgp_attr_origin(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	/* If any recognized attribute has Attribute Length that conflicts
     with the expected length (based on the attribute type code), then
     the Error Subcode is set to Attribute Length Error.  The Data
     field contains the erroneous attribute (type, length and
     value). */
	if(length != 1) {
		zlog(peer->log, LOG_ERR, "Origin attribute length is not one %d", length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	/* Fetch origin attribute. */
	attr->origin = stream_getc(BGP_INPUT(peer));

	/* If the ORIGIN attribute has an undefined value, then the Error
     Subcode is set to Invalid Origin Attribute.  The Data field
     contains the unrecognized attribute (type, length and value). */
	if((attr->origin != BGP_ORIGIN_IGP) && (attr->origin != BGP_ORIGIN_EGP) && (attr->origin != BGP_ORIGIN_INCOMPLETE)) {
		zlog(peer->log, LOG_ERR, "Origin attribute value is invalid %d", attr->origin);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_INVAL_ORIGIN, args->total);
	}

	/* Set oring attribute flag. */
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_ORIGIN);

	return 0;
}

/* Parse AS path information.  This function is wrapper of
   aspath_parse. */
static int bgp_attr_aspath(struct bgp_attr_parser_args *args) {
	struct attr *const attr = args->attr;
	struct peer *const peer = args->peer;
	const bgp_size_t length = args->length;

	/*
   * peer with AS4 => will get 4Byte ASnums
   * otherwise, will get 16 Bit
   */
	attr->aspath = aspath_parse(peer->ibuf, length, CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV));

	/* In case of IBGP, length will be zero. */
	if(!attr->aspath) {
		zlog(peer->log, LOG_ERR, "Malformed AS path from %s, length is %d", peer->host, length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_MAL_AS_PATH, 0);
	}

	/* Set aspath attribute flag. */
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_AS_PATH);

	return BGP_ATTR_PARSE_PROCEED;
}

static bgp_attr_parse_ret_t bgp_attr_aspath_check(struct peer *const peer, struct attr *const attr) {
	/* These checks were part of bgp_attr_aspath, but with
   * as4 we should to check aspath things when
   * aspath synthesizing with as4_path has already taken place.
   * Otherwise we check ASPATH and use the synthesized thing, and that is
   * not right.
   * So do the checks later, i.e. here
   */
	struct bgp *bgp = peer->bgp;
	struct aspath *aspath;

	/* Confederation sanity check. */
	if((peer->sort == BGP_PEER_CONFED && !aspath_left_confed_check(attr->aspath)) || (peer->sort == BGP_PEER_EBGP && aspath_confed_check(attr->aspath))) {
		zlog(peer->log, LOG_ERR, "Malformed AS path from %s", peer->host);
		bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_MAL_AS_PATH);
		return BGP_ATTR_PARSE_ERROR;
	}

	/* First AS check for EBGP. */
	if(bgp != NULL && bgp_flag_check(bgp, BGP_FLAG_ENFORCE_FIRST_AS)) {
		if(peer->sort == BGP_PEER_EBGP && !aspath_firstas_check(attr->aspath, peer->as)) {
			zlog(peer->log, LOG_ERR, "%s incorrect first AS (must be %u)", peer->host, peer->as);
			bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_MAL_AS_PATH);
			return BGP_ATTR_PARSE_ERROR;
		}
	}

	/* local-as prepend */
	if(peer->change_local_as && !CHECK_FLAG(peer->flags, PEER_FLAG_LOCAL_AS_NO_PREPEND)) {
		aspath = aspath_dup(attr->aspath);
		aspath = aspath_add_seq(aspath, peer->change_local_as);
		aspath_unintern(&attr->aspath);
		attr->aspath = aspath_intern(aspath);
	}

	return BGP_ATTR_PARSE_PROCEED;
}

/* Parse AS4 path information.  This function is another wrapper of
   aspath_parse. */
static int bgp_attr_as4_path(struct bgp_attr_parser_args *args, struct aspath **as4_path) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	*as4_path = aspath_parse(peer->ibuf, length, 1);

	/* In case of IBGP, length will be zero. */
	if(!*as4_path) {
		zlog(peer->log, LOG_ERR, "Malformed AS4 path from %s, length is %d", peer->host, length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_MAL_AS_PATH, 0);
	}

	/* Set aspath attribute flag. */
	if(as4_path) {
		attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_AS4_PATH);
	}

	return BGP_ATTR_PARSE_PROCEED;
}

/* Nexthop attribute. */
static bgp_attr_parse_ret_t bgp_attr_nexthop(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	in_addr_t nexthop_h, nexthop_n;

	/* Check nexthop attribute length. */
	if(length != 4) {
		zlog(peer->log, LOG_ERR, "Nexthop attribute length isn't four [%d]", length);

		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	/* According to section 6.3 of RFC4271, syntactically incorrect NEXT_HOP
     attribute must result in a NOTIFICATION message (this is implemented below).
     At the same time, semantically incorrect NEXT_HOP is more likely to be just
     logged locally (this is implemented somewhere else). The UPDATE message
     gets ignored in any of these cases. */
	nexthop_n = stream_get_ipv4(peer->ibuf);
	nexthop_h = ntohl(nexthop_n);
	if((IPV4_NET0(nexthop_h) || IPV4_NET127(nexthop_h) || IPV4_CLASS_DE(nexthop_h)) && !BGP_DEBUG(allow_martians, ALLOW_MARTIANS)) /* loopbacks may be used in testing */
	{
		char buf[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &nexthop_n, buf, INET_ADDRSTRLEN);
		zlog(peer->log, LOG_ERR, "Martian nexthop %s", buf);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_INVAL_NEXT_HOP, args->total);
	}

	attr->nexthop.s_addr = nexthop_n;
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP);

	return BGP_ATTR_PARSE_PROCEED;
}

/* MED atrribute. */
static bgp_attr_parse_ret_t bgp_attr_med(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	/* Length check. */
	if(length != 4) {
		zlog(peer->log, LOG_ERR, "MED attribute length isn't four [%d]", length);

		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	attr->med = stream_getl(peer->ibuf);

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_MULTI_EXIT_DISC);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Local preference attribute. */
static bgp_attr_parse_ret_t bgp_attr_local_pref(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	/* Length check. */
	if(length != 4) {
		zlog(peer->log, LOG_ERR, "LOCAL_PREF attribute length isn't 4 [%u]", length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	/* If it is contained in an UPDATE message that is received from an
     external peer, then this attribute MUST be ignored by the
     receiving speaker. */
	if(peer->sort == BGP_PEER_EBGP) {
		stream_forward_getp(peer->ibuf, length);
		return BGP_ATTR_PARSE_PROCEED;
	}

	attr->local_pref = stream_getl(peer->ibuf);

	/* Set atomic aggregate flag. */
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_LOCAL_PREF);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Atomic aggregate. */
static int bgp_attr_atomic(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	/* Length check. */
	if(length != 0) {
		zlog(peer->log, LOG_ERR, "ATOMIC_AGGREGATE attribute length isn't 0 [%u]", length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	/* Set atomic aggregate flag. */
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_ATOMIC_AGGREGATE);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Aggregator attribute */
static int bgp_attr_aggregator(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	int wantedlen = 6;
	struct attr_extra *attre = bgp_attr_extra_get(attr);

	/* peer with AS4 will send 4 Byte AS, peer without will send 2 Byte */
	if(CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV)) {
		wantedlen = 8;
	}

	if(length != wantedlen) {
		zlog(peer->log, LOG_ERR, "AGGREGATOR attribute length isn't %u [%u]", wantedlen, length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	if(CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV)) {
		attre->aggregator_as = stream_getl(peer->ibuf);
	} else {
		attre->aggregator_as = stream_getw(peer->ibuf);
	}
	attre->aggregator_addr.s_addr = stream_get_ipv4(peer->ibuf);

	/* Set atomic aggregate flag. */
	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_AGGREGATOR);

	return BGP_ATTR_PARSE_PROCEED;
}

/* New Aggregator attribute */
static bgp_attr_parse_ret_t bgp_attr_as4_aggregator(struct bgp_attr_parser_args *args, as_t *as4_aggregator_as, struct in_addr *as4_aggregator_addr) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	if(length != 8) {
		zlog(peer->log, LOG_ERR, "New Aggregator length is not 8 [%d]", length);
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, 0);
	}

	*as4_aggregator_as = stream_getl(peer->ibuf);
	as4_aggregator_addr->s_addr = stream_get_ipv4(peer->ibuf);

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_AS4_AGGREGATOR);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Munge Aggregator and New-Aggregator, AS_PATH and NEW_AS_PATH.
 */
static bgp_attr_parse_ret_t bgp_attr_munge_as4_attrs(struct peer *const peer, struct attr *const attr, struct aspath *as4_path, as_t as4_aggregator, struct in_addr *as4_aggregator_addr) {
	int ignore_as4_path = 0;
	struct aspath *newpath;
	struct attr_extra *attre = attr->extra;

	if(!attr->aspath) {
		/* NULL aspath shouldn't be possible as bgp_attr_parse should have
       * checked that all well-known, mandatory attributes were present.
       *
       * Can only be a problem with peer itself - hard error
       */
		return BGP_ATTR_PARSE_ERROR;
	}

	if(CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV)) {
		/* peer can do AS4, so we ignore AS4_PATH and AS4_AGGREGATOR
       * if given.
       * It is worth a warning though, because the peer really
       * should not send them
       */
		if(BGP_DEBUG(as4, AS4)) {
			if(attr->flag & (ATTR_FLAG_BIT(BGP_ATTR_AS4_PATH))) {
				zlog_debug("[AS4] %s %s AS4_PATH", peer->host, "AS4 capable peer, yet it sent");
			}

			if(attr->flag & (ATTR_FLAG_BIT(BGP_ATTR_AS4_AGGREGATOR))) {
				zlog_debug("[AS4] %s %s AS4_AGGREGATOR", peer->host, "AS4 capable peer, yet it sent");
			}
		}

		return BGP_ATTR_PARSE_PROCEED;
	}

	/* We have a asn16 peer.  First, look for AS4_AGGREGATOR
   * because that may override AS4_PATH
   */
	if(attr->flag & (ATTR_FLAG_BIT(BGP_ATTR_AS4_AGGREGATOR))) {
		if(attr->flag & (ATTR_FLAG_BIT(BGP_ATTR_AGGREGATOR))) {
			assert(attre);

			/* received both.
           * if the as_number in aggregator is not AS_TRANS,
           *  then AS4_AGGREGATOR and AS4_PATH shall be ignored
           *        and the Aggregator shall be taken as
           *        info on the aggregating node, and the AS_PATH
           *        shall be taken as the AS_PATH
           *  otherwise
           *        the Aggregator shall be ignored and the
           *        AS4_AGGREGATOR shall be taken as the
           *        Aggregating node and the AS_PATH is to be
           *        constructed "as in all other cases"
           */
			if(attre->aggregator_as != BGP_AS_TRANS) {
				/* ignore */
				if(BGP_DEBUG(as4, AS4)) {
					zlog_debug(
						"[AS4] %s BGP not AS4 capable peer"
						" send AGGREGATOR != AS_TRANS and"
						" AS4_AGGREGATOR, so ignore"
						" AS4_AGGREGATOR and AS4_PATH",
						peer->host
					);
				}
				ignore_as4_path = 1;
			} else {
				/* "New_aggregator shall be taken as aggregator" */
				attre->aggregator_as = as4_aggregator;
				attre->aggregator_addr.s_addr = as4_aggregator_addr->s_addr;
			}
		} else {
			/* We received a AS4_AGGREGATOR but no AGGREGATOR.
           * That is bogus - but reading the conditions
           * we have to handle AS4_AGGREGATOR as if it were
           * AGGREGATOR in that case
           */
			if(BGP_DEBUG(as4, AS4)) {
				zlog_debug(
					"[AS4] %s BGP not AS4 capable peer send"
					" AS4_AGGREGATOR but no AGGREGATOR, will take"
					" it as if AGGREGATOR with AS_TRANS had been there",
					peer->host
				);
			}
			(attre = bgp_attr_extra_get(attr))->aggregator_as = as4_aggregator;
			/* sweep it under the carpet and simulate a "good" AGGREGATOR */
			attr->flag |= (ATTR_FLAG_BIT(BGP_ATTR_AGGREGATOR));
		}
	}

	/* need to reconcile NEW_AS_PATH and AS_PATH */
	if(!ignore_as4_path && (attr->flag & (ATTR_FLAG_BIT(BGP_ATTR_AS4_PATH)))) {
		newpath = aspath_reconcile_as4(attr->aspath, as4_path);
		aspath_unintern(&attr->aspath);
		attr->aspath = aspath_intern(newpath);
	}
	return BGP_ATTR_PARSE_PROCEED;
}

/* Community attribute. */
static bgp_attr_parse_ret_t bgp_attr_community(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	if(length == 0) {
		attr->community = NULL;
		return BGP_ATTR_PARSE_PROCEED;
	}

	attr->community = community_parse((u_int32_t *) stream_pnt(peer->ibuf), length);

	/* XXX: fix community_parse to use stream API and remove this */
	stream_forward_getp(peer->ibuf, length);

	if(!attr->community) {
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, args->total);
	}

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_COMMUNITIES);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Originator ID attribute. */
static bgp_attr_parse_ret_t bgp_attr_originator_id(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	/* Length check. */
	if(length != 4) {
		zlog(peer->log, LOG_ERR, "Bad originator ID length %d", length);

		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	(bgp_attr_extra_get(attr))->originator_id.s_addr = stream_get_ipv4(peer->ibuf);

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_ORIGINATOR_ID);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Cluster list attribute. */
static bgp_attr_parse_ret_t bgp_attr_cluster_list(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	/* Check length. */
	if(length % 4) {
		zlog(peer->log, LOG_ERR, "Bad cluster list length %d", length);

		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, args->total);
	}

	(bgp_attr_extra_get(attr))->cluster = cluster_parse((struct in_addr *) stream_pnt(peer->ibuf), length);

	/* XXX: Fix cluster_parse to use stream API and then remove this */
	stream_forward_getp(peer->ibuf, length);

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_CLUSTER_LIST);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Multiprotocol reachability information parse. */
int bgp_mp_reach_parse(struct bgp_attr_parser_args *args, struct bgp_nlri *mp_update) {
	afi_t afi;
	safi_t safi;
	bgp_size_t nlri_len;
	size_t start;
	struct stream *s;
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;
	struct attr_extra *attre = bgp_attr_extra_get(attr);

	/* Set end of packet. */
	s = BGP_INPUT(peer);
	start = stream_get_getp(s);

	/* safe to read statically sized header? */
#define BGP_MP_REACH_MIN_SIZE 5
#define LEN_LEFT (length - (stream_get_getp(s) - start))
	if((length > STREAM_READABLE(s)) || (length < BGP_MP_REACH_MIN_SIZE)) {
		zlog_info("%s: %s sent invalid length, %lu", __func__, peer->host, (unsigned long) length);
		return BGP_ATTR_PARSE_ERROR_NOTIFYPLS;
	}

	/* Load AFI, SAFI. */
	afi = stream_getw(s);
	safi = stream_getc(s);

	/* Get nexthop length. */
	attre->mp_nexthop_len = stream_getc(s);

	if(LEN_LEFT < attre->mp_nexthop_len) {
		zlog_info("%s: %s, MP nexthop length, %u, goes past end of attribute", __func__, peer->host, attre->mp_nexthop_len);
		return BGP_ATTR_PARSE_ERROR_NOTIFYPLS;
	}

	/* Nexthop length check. */
	switch(attre->mp_nexthop_len) {
		case 4:
			stream_get(&attre->mp_nexthop_global_in, s, 4);
			/* Probably needed for RFC 2283 */
			if(attr->nexthop.s_addr == 0) {
				memcpy(&attr->nexthop.s_addr, &attre->mp_nexthop_global_in, 4);
			}
			break;
		case 12:
			stream_getl(s); /* RD high */
			stream_getl(s); /* RD low */
			stream_get(&attre->mp_nexthop_global_in, s, 4);
			break;
		case 24:
			{
				u_int32_t rd_high __attribute__((unused));
				u_int32_t rd_low __attribute__((unused));

				rd_high = stream_getl(s);
				rd_low = stream_getl(s);
			}
			/* fall through */
		case 16: stream_get(&attre->mp_nexthop_global, s, 16); break;
		case 32:
		case 48:
			if(attre->mp_nexthop_len == 48) {
				u_int32_t rd_high __attribute__((unused));
				u_int32_t rd_low __attribute__((unused));

				rd_high = stream_getl(s);
				rd_low = stream_getl(s);
			}
			stream_get(&attre->mp_nexthop_global, s, 16);

			if(attre->mp_nexthop_len == 48) {
				u_int32_t rd_high __attribute__((unused));
				u_int32_t rd_low __attribute__((unused));

				rd_high = stream_getl(s);
				rd_low = stream_getl(s);
			}
			stream_get(&attre->mp_nexthop_local, s, 16);
			if(!IN6_IS_ADDR_LINKLOCAL(&attre->mp_nexthop_local)) {
				char buf1[INET6_ADDRSTRLEN];
				char buf2[INET6_ADDRSTRLEN];

				if(BGP_DEBUG(update, UPDATE_IN)) {
					zlog_debug(
						"%s got two nexthop %s %s but second one is not a link-local nexthop", peer->host, inet_ntop(AF_INET6, &attre->mp_nexthop_global, buf1, INET6_ADDRSTRLEN),
						inet_ntop(AF_INET6, &attre->mp_nexthop_local, buf2, INET6_ADDRSTRLEN)
					);
				}

				attre->mp_nexthop_len = 16;
			}
			break;
		default: zlog_info("%s: (%s) Wrong multiprotocol next hop length: %d", __func__, peer->host, attre->mp_nexthop_len); return BGP_ATTR_PARSE_ERROR_NOTIFYPLS;
	}

	if(!LEN_LEFT) {
		zlog_info("%s: (%s) Failed to read SNPA and NLRI(s)", __func__, peer->host);
		return BGP_ATTR_PARSE_ERROR_NOTIFYPLS;
	}

	{
		u_char val;
		if((val = stream_getc(s))) {
			zlog_warn("%s sent non-zero value, %u, for defunct SNPA-length field", peer->host, val);
		}
	}

	/* must have nrli_len, what is left of the attribute */
	nlri_len = LEN_LEFT;
	if((!nlri_len) || (nlri_len > STREAM_READABLE(s))) {
		zlog_info("%s: (%s) Failed to read NLRI", __func__, peer->host);
		return BGP_ATTR_PARSE_ERROR_NOTIFYPLS;
	}

	mp_update->afi = afi;
	mp_update->safi = safi;
	mp_update->nlri = stream_pnt(s);
	mp_update->length = nlri_len;

	stream_forward_getp(s, nlri_len);

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_MP_REACH_NLRI);

	return BGP_ATTR_PARSE_PROCEED;
#undef LEN_LEFT
}

/* Multiprotocol unreachable parse */
int bgp_mp_unreach_parse(struct bgp_attr_parser_args *args, struct bgp_nlri *mp_withdraw) {
	struct stream *s;
	afi_t afi;
	safi_t safi;
	u_int16_t withdraw_len;
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	s = peer->ibuf;

#define BGP_MP_UNREACH_MIN_SIZE 3
	if((length > STREAM_READABLE(s)) || (length < BGP_MP_UNREACH_MIN_SIZE)) {
		return BGP_ATTR_PARSE_ERROR_NOTIFYPLS;
	}

	afi = stream_getw(s);
	safi = stream_getc(s);

	withdraw_len = length - BGP_MP_UNREACH_MIN_SIZE;

	mp_withdraw->afi = afi;
	mp_withdraw->safi = safi;
	mp_withdraw->nlri = stream_pnt(s);
	mp_withdraw->length = withdraw_len;

	stream_forward_getp(s, withdraw_len);

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_MP_UNREACH_NLRI);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Large Community attribute. */
static bgp_attr_parse_ret_t bgp_attr_large_community(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	if(length == 0) {
		if(attr->extra) {
			attr->extra->lcommunity = NULL;
		}
		/* Empty extcomm doesn't seem to be invalid per se */
		return BGP_ATTR_PARSE_PROCEED;
	}

	(bgp_attr_extra_get(attr))->lcommunity = lcommunity_parse((u_int8_t *) stream_pnt(peer->ibuf), length);
	/* XXX: fix ecommunity_parse to use stream API */
	stream_forward_getp(peer->ibuf, length);

	if(attr->extra && !attr->extra->lcommunity) {
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, args->total);
	}

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_LARGE_COMMUNITIES);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Extended Community attribute. */
static bgp_attr_parse_ret_t bgp_attr_ext_communities(struct bgp_attr_parser_args *args) {
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	const bgp_size_t length = args->length;

	if(length == 0) {
		if(attr->extra) {
			attr->extra->ecommunity = NULL;
		}
		/* Empty extcomm doesn't seem to be invalid per se */
		return BGP_ATTR_PARSE_PROCEED;
	}

	(bgp_attr_extra_get(attr))->ecommunity = ecommunity_parse((u_int8_t *) stream_pnt(peer->ibuf), length);
	/* XXX: fix ecommunity_parse to use stream API */
	stream_forward_getp(peer->ibuf, length);

	if(attr->extra && !attr->extra->ecommunity) {
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, args->total);
	}

	attr->flag |= ATTR_FLAG_BIT(BGP_ATTR_EXT_COMMUNITIES);

	return BGP_ATTR_PARSE_PROCEED;
}

/* Parse Tunnel Encap attribute in an UPDATE */
static int bgp_attr_encap(
	uint8_t type, struct peer *peer, /* IN */
	bgp_size_t length,		 /* IN: attr's length field */
	struct attr *attr,		 /* IN: caller already allocated */
	u_char flag,			 /* IN: attr's flags field */
	u_char *startp
) {
	bgp_size_t total;
	struct attr_extra *attre = NULL;
	struct bgp_attr_encap_subtlv *stlv_last = NULL;
	uint16_t tunneltype;

	total = length + (CHECK_FLAG(flag, BGP_ATTR_FLAG_EXTLEN) ? 4 : 3);

	if(!CHECK_FLAG(flag, BGP_ATTR_FLAG_TRANS) || !CHECK_FLAG(flag, BGP_ATTR_FLAG_OPTIONAL)) {
		zlog(peer->log, LOG_ERR, "Tunnel Encap attribute flag isn't optional and transitive %d", flag);
		bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_ATTR_FLAG_ERR, startp, total);
		return -1;
	}

	if(BGP_ATTR_ENCAP == type) {
		/* read outer TLV type and length */
		uint16_t tlv_length;

		if(length < 4) {
			zlog(peer->log, LOG_ERR, "Tunnel Encap attribute not long enough to contain outer T,L");
			bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, startp, total);
			return -1;
		}
		tunneltype = stream_getw(BGP_INPUT(peer));
		tlv_length = stream_getw(BGP_INPUT(peer));
		length -= 4;

		if(tlv_length != length) {
			zlog(peer->log, LOG_ERR, "%s: tlv_length(%d) != length(%d)", __func__, tlv_length, length);
		}
	}

	while(length >= 4) {
		uint16_t subtype = 0;
		uint16_t sublength = 0;
		struct bgp_attr_encap_subtlv *tlv;

		if(BGP_ATTR_ENCAP == type) {
			subtype = stream_getc(BGP_INPUT(peer));
			sublength = stream_getc(BGP_INPUT(peer));
			length -= 2;
		}

		if(sublength > length) {
			zlog(peer->log, LOG_ERR, "Tunnel Encap attribute sub-tlv length %d exceeds remaining length %d", sublength, length);
			bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, startp, total);
			return -1;
		}

		/* alloc and copy sub-tlv */
		/* TBD make sure these are freed when attributes are released */
		tlv = XCALLOC(MTYPE_ENCAP_TLV, sizeof(struct bgp_attr_encap_subtlv) - 1 + sublength);
		tlv->type = subtype;
		tlv->length = sublength;
		stream_get(tlv->value, peer->ibuf, sublength);
		length -= sublength;

		/* attach tlv to encap chain */
		if(!attre) {
			attre = bgp_attr_extra_get(attr);
			if(BGP_ATTR_ENCAP == type) {
				for(stlv_last = attre->encap_subtlvs; stlv_last && stlv_last->next; stlv_last = stlv_last->next)
					;
				if(stlv_last) {
					stlv_last->next = tlv;
				} else {
					attre->encap_subtlvs = tlv;
				}
			}
		} else {
			stlv_last->next = tlv;
		}
		stlv_last = tlv;
	}

	if(attre && (BGP_ATTR_ENCAP == type)) {
		attre->encap_tunneltype = tunneltype;
	}

	if(length) {
		/* spurious leftover data */
		zlog(peer->log, LOG_ERR, "Tunnel Encap attribute length is bad: %d leftover octets", length);
		bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, startp, total);
		return -1;
	}

	return 0;
}

/* BGP unknown attribute treatment. */
static bgp_attr_parse_ret_t bgp_attr_unknown(struct bgp_attr_parser_args *args) {
	bgp_size_t total = args->total;
	struct transit *transit;
	struct attr_extra *attre;
	struct peer *const peer = args->peer;
	struct attr *const attr = args->attr;
	u_char *const startp = args->startp;
	const u_char type = args->type;
	const u_char flag = args->flags;
	const bgp_size_t length = args->length;

	if(BGP_DEBUG(normal, NORMAL)) {
		zlog_debug("%s Unknown attribute is received (type %d, length %d)", peer->host, type, length);
	}

	if(BGP_DEBUG(events, EVENTS)) {
		zlog(peer->log, LOG_DEBUG, "Unknown attribute type %d length %d is received", type, length);
	}

	/* Forward read pointer of input stream. */
	stream_forward_getp(peer->ibuf, length);

	/* If any of the mandatory well-known attributes are not recognized,
     then the Error Subcode is set to Unrecognized Well-known
     Attribute.  The Data field contains the unrecognized attribute
     (type, length and value). */
	if(!CHECK_FLAG(flag, BGP_ATTR_FLAG_OPTIONAL)) {
		return bgp_attr_malformed(args, BGP_NOTIFY_UPDATE_UNREC_ATTR, args->total);
	}

	/* Unrecognized non-transitive optional attributes must be quietly
     ignored and not passed along to other BGP peers. */
	if(!CHECK_FLAG(flag, BGP_ATTR_FLAG_TRANS)) {
		return BGP_ATTR_PARSE_PROCEED;
	}

	/* If a path with recognized transitive optional attribute is
     accepted and passed along to other BGP peers and the Partial bit
     in the Attribute Flags octet is set to 1 by some previous AS, it
     is not set back to 0 by the current AS. */
	SET_FLAG(*startp, BGP_ATTR_FLAG_PARTIAL);

	/* Store transitive attribute to the end of attr->transit. */
	if(!((attre = bgp_attr_extra_get(attr))->transit)) {
		attre->transit = XCALLOC(MTYPE_TRANSIT, sizeof(struct transit));
	}

	transit = attre->transit;

	if(transit->val) {
		transit->val = XREALLOC(MTYPE_TRANSIT_VAL, transit->val, transit->length + total);
	} else {
		transit->val = XMALLOC(MTYPE_TRANSIT_VAL, total);
	}

	memcpy(transit->val + transit->length, startp, total);
	transit->length += total;

	return BGP_ATTR_PARSE_PROCEED;
}

/* Well-known attribute check. */
static int bgp_attr_check(struct peer *peer, struct attr *attr) {
	u_char type = 0;

	/* BGP Graceful-Restart End-of-RIB for IPv4 unicast is signaled as an
   * empty UPDATE.  */
	if(CHECK_FLAG(peer->cap, PEER_CAP_RESTART_RCV) && !attr->flag) {
		return BGP_ATTR_PARSE_PROCEED;
	}

	/* "An UPDATE message that contains the MP_UNREACH_NLRI is not required
     to carry any other path attributes.", though if MP_REACH_NLRI or NLRI
     are present, it should.  Check for any other attribute being present
     instead.
   */
	if(attr->flag == ATTR_FLAG_BIT(BGP_ATTR_MP_UNREACH_NLRI)) {
		return BGP_ATTR_PARSE_PROCEED;
	}

	if(!CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_ORIGIN))) {
		type = BGP_ATTR_ORIGIN;
	}

	if(!CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_AS_PATH))) {
		type = BGP_ATTR_AS_PATH;
	}

	/* RFC 2858 makes Next-Hop optional/ignored, if MP_REACH_NLRI is present and
   * NLRI is empty. We can't easily check NLRI empty here though.
   */
	if(!CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP)) && !CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_MP_REACH_NLRI))) {
		type = BGP_ATTR_NEXT_HOP;
	}

	if(peer->sort == BGP_PEER_IBGP && !CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_LOCAL_PREF))) {
		type = BGP_ATTR_LOCAL_PREF;
	}

	if(type) {
		zlog(peer->log, LOG_WARNING, "%s Missing well-known attribute %d / %s", peer->host, type, LOOKUP(attr_str, type));
		bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_MISS_ATTR, &type, 1);
		return BGP_ATTR_PARSE_ERROR;
	}
	return BGP_ATTR_PARSE_PROCEED;
}

/* Read attribute of update packet.  This function is called from
   bgp_update_receive() in bgp_packet.c.  */
bgp_attr_parse_ret_t bgp_attr_parse(struct peer *peer, struct attr *attr, bgp_size_t size, struct bgp_nlri *mp_update, struct bgp_nlri *mp_withdraw) {
	int ret;
	u_char flag = 0;
	u_char type = 0;
	bgp_size_t length;
	u_char *startp, *endp;
	u_char *attr_endp;
	u_char seen[BGP_ATTR_BITMAP_SIZE];
	/* we need the as4_path only until we have synthesized the as_path with it */
	/* same goes for as4_aggregator */
	struct aspath *as4_path = NULL;
	as_t as4_aggregator = 0;
	struct in_addr as4_aggregator_addr = { .s_addr = 0 };

	/* Initialize bitmap. */
	memset(seen, 0, BGP_ATTR_BITMAP_SIZE);

	/* End pointer of BGP attribute. */
	assert(size <= stream_get_size(BGP_INPUT(peer)));
	assert(size <= stream_get_endp(BGP_INPUT(peer)));
	endp = BGP_INPUT_PNT(peer) + size;

	/* Get attributes to the end of attribute length. */
	while(BGP_INPUT_PNT(peer) < endp) {
		/* Check remaining length check.*/
		if(endp - BGP_INPUT_PNT(peer) < BGP_ATTR_MIN_LEN) {
			/* XXX warning: long int format, int arg (arg 5) */
			zlog(peer->log, LOG_WARNING, "%s: error BGP attribute length %lu is smaller than min len", peer->host, (unsigned long) (endp - STREAM_PNT(BGP_INPUT(peer))));

			bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR);
			return BGP_ATTR_PARSE_ERROR;
		}

		/* Fetch attribute flag and type. */
		startp = BGP_INPUT_PNT(peer);
		/* "The lower-order four bits of the Attribute Flags octet are
         unused.  They MUST be zero when sent and MUST be ignored when
         received." */
		flag = 0xF0 & stream_getc(BGP_INPUT(peer));
		type = stream_getc(BGP_INPUT(peer));

		/* Check whether Extended-Length applies and is in bounds */
		if(CHECK_FLAG(flag, BGP_ATTR_FLAG_EXTLEN) && ((endp - startp) < (BGP_ATTR_MIN_LEN + 1))) {
			zlog(peer->log, LOG_WARNING, "%s: Extended length set, but just %lu bytes of attr header", peer->host, (unsigned long) (endp - STREAM_PNT(BGP_INPUT(peer))));

			bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR);
			return BGP_ATTR_PARSE_ERROR;
		}

		/* Check extended attribue length bit. */
		if(CHECK_FLAG(flag, BGP_ATTR_FLAG_EXTLEN)) {
			length = stream_getw(BGP_INPUT(peer));
		} else {
			length = stream_getc(BGP_INPUT(peer));
		}

		/* If any attribute appears more than once in the UPDATE
	 message, then the Error Subcode is set to Malformed Attribute
	 List. */

		if(CHECK_BITMAP(seen, type)) {
			zlog(peer->log, LOG_WARNING, "%s: error BGP attribute type %d appears twice in a message", peer->host, type);

			bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_MAL_ATTR);
			return BGP_ATTR_PARSE_ERROR;
		}

		/* Set type to bitmap to check duplicate attribute.  `type' is
	 unsigned char so it never overflow bitmap range. */

		SET_BITMAP(seen, type);

		/* Overflow check. */
		attr_endp = BGP_INPUT_PNT(peer) + length;

		if(attr_endp > endp) {
			zlog(peer->log, LOG_WARNING, "%s: BGP type %d length %d is too large, attribute total length is %d.  attr_endp is %p.  endp is %p", peer->host, type, length, size, attr_endp, endp);
			zlog_warn("%s: BGP type %d length %d is too large, attribute total length is %d.  attr_endp is %p.  endp is %p", peer->host, type, length, size, attr_endp, endp);
			bgp_notify_send_with_data(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, startp, endp - startp);
			return BGP_ATTR_PARSE_ERROR;
		}

		struct bgp_attr_parser_args attr_args = {
			.peer = peer,
			.length = length,
			.attr = attr,
			.type = type,
			.flags = flag,
			.startp = startp,
			.total = attr_endp - startp,
		};

		/* If any recognized attribute has Attribute Flags that conflict
         with the Attribute Type Code, then the Error Subcode is set to
         Attribute Flags Error.  The Data field contains the erroneous
         attribute (type, length and value). */
		if(bgp_attr_flag_invalid(&attr_args)) {
			bgp_attr_parse_ret_t ret;
			ret = bgp_attr_malformed(&attr_args, BGP_NOTIFY_UPDATE_ATTR_FLAG_ERR, attr_args.total);
			if(ret == BGP_ATTR_PARSE_PROCEED) {
				continue;
			}
			return ret;
		}

		/* OK check attribute and store it's value. */
		switch(type) {
			case BGP_ATTR_ORIGIN: ret = bgp_attr_origin(&attr_args); break;
			case BGP_ATTR_AS_PATH: ret = bgp_attr_aspath(&attr_args); break;
			case BGP_ATTR_AS4_PATH: ret = bgp_attr_as4_path(&attr_args, &as4_path); break;
			case BGP_ATTR_NEXT_HOP: ret = bgp_attr_nexthop(&attr_args); break;
			case BGP_ATTR_MULTI_EXIT_DISC: ret = bgp_attr_med(&attr_args); break;
			case BGP_ATTR_LOCAL_PREF: ret = bgp_attr_local_pref(&attr_args); break;
			case BGP_ATTR_ATOMIC_AGGREGATE: ret = bgp_attr_atomic(&attr_args); break;
			case BGP_ATTR_AGGREGATOR: ret = bgp_attr_aggregator(&attr_args); break;
			case BGP_ATTR_AS4_AGGREGATOR: ret = bgp_attr_as4_aggregator(&attr_args, &as4_aggregator, &as4_aggregator_addr); break;
			case BGP_ATTR_COMMUNITIES: ret = bgp_attr_community(&attr_args); break;
			case BGP_ATTR_LARGE_COMMUNITIES: ret = bgp_attr_large_community(&attr_args); break;
			case BGP_ATTR_ORIGINATOR_ID: ret = bgp_attr_originator_id(&attr_args); break;
			case BGP_ATTR_CLUSTER_LIST: ret = bgp_attr_cluster_list(&attr_args); break;
			case BGP_ATTR_MP_REACH_NLRI: ret = bgp_mp_reach_parse(&attr_args, mp_update); break;
			case BGP_ATTR_MP_UNREACH_NLRI: ret = bgp_mp_unreach_parse(&attr_args, mp_withdraw); break;
			case BGP_ATTR_EXT_COMMUNITIES: ret = bgp_attr_ext_communities(&attr_args); break;
			case BGP_ATTR_ENCAP: ret = bgp_attr_encap(type, peer, length, attr, flag, startp); break;
			default: ret = bgp_attr_unknown(&attr_args); break;
		}

		if(ret == BGP_ATTR_PARSE_ERROR_NOTIFYPLS) {
			bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_MAL_ATTR);
			ret = BGP_ATTR_PARSE_ERROR;
		}

		/* If hard error occurred immediately return to the caller. */
		if(ret == BGP_ATTR_PARSE_ERROR) {
			zlog(peer->log, LOG_WARNING, "%s: Attribute %s, parse error", peer->host, LOOKUP(attr_str, type));
			if(as4_path) {
				aspath_unintern(&as4_path);
			}
			return ret;
		}
		if(ret == BGP_ATTR_PARSE_WITHDRAW) {
			zlog(peer->log, LOG_WARNING, "%s: Attribute %s, parse error - treating as withdrawal", peer->host, LOOKUP(attr_str, type));
			if(as4_path) {
				aspath_unintern(&as4_path);
			}
			return ret;
		}

		/* Check the fetched length. */
		if(BGP_INPUT_PNT(peer) != attr_endp) {
			zlog(peer->log, LOG_WARNING, "%s: BGP attribute %s, fetch error", peer->host, LOOKUP(attr_str, type));
			bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR);
			if(as4_path) {
				aspath_unintern(&as4_path);
			}
			return BGP_ATTR_PARSE_ERROR;
		}
	}
	/* Check final read pointer is same as end pointer. */
	if(BGP_INPUT_PNT(peer) != endp) {
		zlog(peer->log, LOG_WARNING, "%s: BGP attribute %s, length mismatch", peer->host, LOOKUP(attr_str, type));
		bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_ATTR_LENG_ERR);
		if(as4_path) {
			aspath_unintern(&as4_path);
		}
		return BGP_ATTR_PARSE_ERROR;
	}

	/* Check all mandatory well-known attributes are present */
	{
		bgp_attr_parse_ret_t ret;
		if((ret = bgp_attr_check(peer, attr)) < 0) {
			if(as4_path) {
				aspath_unintern(&as4_path);
			}
			return ret;
		}
	}

	/*
   * At this place we can see whether we got AS4_PATH and/or
   * AS4_AGGREGATOR from a 16Bit peer and act accordingly.
   * We can not do this before we've read all attributes because
   * the as4 handling does not say whether AS4_PATH has to be sent
   * after AS_PATH or not - and when AS4_AGGREGATOR will be send
   * in relationship to AGGREGATOR.
   * So, to be defensive, we are not relying on any order and read
   * all attributes first, including these 32bit ones, and now,
   * afterwards, we look what and if something is to be done for as4.
   *
   * It is possible to not have AS_PATH, e.g. GR EoR and sole
   * MP_UNREACH_NLRI.
   */
	/* actually... this doesn't ever return failure currently, but
   * better safe than sorry */
	if(CHECK_FLAG(attr->flag, ATTR_FLAG_BIT(BGP_ATTR_AS_PATH)) && bgp_attr_munge_as4_attrs(peer, attr, as4_path, as4_aggregator, &as4_aggregator_addr)) {
		bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR, BGP_NOTIFY_UPDATE_MAL_ATTR);
		if(as4_path) {
			aspath_unintern(&as4_path);
		}
		return BGP_ATTR_PARSE_ERROR;
	}

	/* At this stage, we have done all fiddling with as4, and the
   * resulting info is in attr->aggregator resp. attr->aspath
   * so we can chuck as4_aggregator and as4_path alltogether in
   * order to save memory
   */
	if(as4_path) {
		aspath_unintern(&as4_path); /* unintern - it is in the hash */
		/* The flag that we got this is still there, but that does not
       * do any trouble
       */
	}
	/*
   * The "rest" of the code does nothing with as4_aggregator.
   * there is no memory attached specifically which is not part
   * of the attr.
   * so ignoring just means do nothing.
   */
	/*
   * Finally do the checks on the aspath we did not do yet
   * because we waited for a potentially synthesized aspath.
   */
	if(attr->flag & (ATTR_FLAG_BIT(BGP_ATTR_AS_PATH))) {
		ret = bgp_attr_aspath_check(peer, attr);
		if(ret != BGP_ATTR_PARSE_PROCEED) {
			return ret;
		}
	}

	/* Finally intern unknown attribute. */
	if(attr->extra && attr->extra->transit) {
		attr->extra->transit = transit_intern(attr->extra->transit);
	}

	return BGP_ATTR_PARSE_PROCEED;
}

int stream_put_prefix(struct stream *, struct prefix *);

size_t bgp_packet_mpattr_start(struct stream *s, afi_t afi, safi_t safi, struct attr *attr) {
	size_t sizep;

	/* Set extended bit always to encode the attribute length as 2 bytes */
	stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTLEN);
	stream_putc(s, BGP_ATTR_MP_REACH_NLRI);
	sizep = stream_get_endp(s);
	stream_putw(s, 0); /* Marker: Attribute length. */

	stream_putw(s, afi);
	stream_putc(s, (safi == SAFI_MPLS_VPN) ? SAFI_MPLS_LABELED_VPN : safi);

	/* Nexthop */
	switch(afi) {
		case AFI_IP:
			switch(safi) {
				case SAFI_MULTICAST:
					stream_putc(s, 4);
					stream_put_ipv4(s, attr->nexthop.s_addr);
					break;
				case SAFI_MPLS_VPN:
					stream_putc(s, 12);
					stream_putl(s, 0); /* RD = 0, per RFC */
					stream_putl(s, 0);
					stream_put(s, &attr->extra->mp_nexthop_global_in, 4);
					break;
				case SAFI_ENCAP:
					stream_putc(s, 4);
					stream_put(s, &attr->extra->mp_nexthop_global_in, 4);
					break;
				case SAFI_UNICAST: /* invalid for IPv4 */
				default: break;
			}
			break;
		case AFI_IP6:
			switch(safi) {
				case SAFI_UNICAST:
				case SAFI_MULTICAST:
					{
						struct attr_extra *attre = attr->extra;

						assert(attr->extra);
						stream_putc(s, attre->mp_nexthop_len);
						stream_put(s, &attre->mp_nexthop_global, 16);
						if(attre->mp_nexthop_len == 32) {
							stream_put(s, &attre->mp_nexthop_local, 16);
						}
					}
					break;
				case SAFI_MPLS_VPN:
					{
						struct attr_extra *attre = attr->extra;

						assert(attr->extra);
						if(attre->mp_nexthop_len == 16) {
							stream_putc(s, 24);
							stream_putl(s, 0); /* RD = 0, per RFC */
							stream_putl(s, 0);
							stream_put(s, &attre->mp_nexthop_global, 16);
						} else if(attre->mp_nexthop_len == 32) {
							stream_putc(s, 48);
							stream_putl(s, 0); /* RD = 0, per RFC */
							stream_putl(s, 0);
							stream_put(s, &attre->mp_nexthop_global, 16);
							stream_putl(s, 0); /* RD = 0, per RFC */
							stream_putl(s, 0);
							stream_put(s, &attre->mp_nexthop_local, 16);
						}
					}
					break;
				case SAFI_ENCAP:
					assert(attr->extra);
					stream_putc(s, 16);
					stream_put(s, &attr->extra->mp_nexthop_global, 16);
					break;
				default: break;
			}
			break;
		default: break;
	}

	/* SNPA */
	stream_putc(s, 0);
	return sizep;
}

void bgp_packet_mpattr_prefix(struct stream *s, afi_t afi, safi_t safi, struct prefix *p, struct prefix_rd *prd, u_char *tag) {
	if(safi == SAFI_MPLS_VPN) {
		/* Tag, RD, Prefix write. */
		stream_putc(s, p->prefixlen + 88);
		stream_put(s, tag, 3);
		stream_put(s, prd->val, 8);
		stream_put(s, &p->u.prefix, PSIZE(p->prefixlen));
	} else {
		stream_put_prefix(s, p);
	}
}

size_t bgp_packet_mpattr_prefix_size(afi_t afi, safi_t safi, struct prefix *p) {
	int size = PSIZE(p->prefixlen);
	if(safi == SAFI_MPLS_VPN) {
		size += 88;
	}
	return size;
}

/*
 * Encodes the tunnel encapsulation attribute
 */
static void bgp_packet_mpattr_tea(struct bgp *bgp, struct peer *peer, struct stream *s, struct attr *attr, uint8_t attrtype) {
	unsigned int attrlenfield = 0;
	unsigned int attrhdrlen = 0;
	struct bgp_attr_encap_subtlv *subtlvs;
	struct bgp_attr_encap_subtlv *st;
	const char *attrname;

	if(!attr || !attr->extra) {
		return;
	}

	switch(attrtype) {
		case BGP_ATTR_ENCAP:
			attrname = "Tunnel Encap";
			subtlvs = attr->extra->encap_subtlvs;

			/*
	     * The tunnel encap attr has an "outer" tlv.
	     * T = tunneltype,
	     * L = total length of subtlvs,
	     * V = concatenated subtlvs.
	     */
			attrlenfield = 2 + 2; /* T + L */
			attrhdrlen = 1 + 1;   /* subTLV T + L */
			break;

		default: assert(0);
	}

	/* if no tlvs, don't make attr */
	if(subtlvs == NULL) {
		return;
	}

	/* compute attr length */
	for(st = subtlvs; st; st = st->next) {
		attrlenfield += (attrhdrlen + st->length);
	}

	if(attrlenfield > 0xffff) {
		zlog(peer->log, LOG_ERR, "%s attribute is too long (length=%d), can't send it", attrname, attrlenfield);
		return;
	}

	if(attrlenfield > 0xff) {
		/* 2-octet length field */
		stream_putc(s, BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTLEN);
		stream_putc(s, attrtype);
		stream_putw(s, attrlenfield & 0xffff);
	} else {
		/* 1-octet length field */
		stream_putc(s, BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_OPTIONAL);
		stream_putc(s, attrtype);
		stream_putc(s, attrlenfield & 0xff);
	}

	if(attrtype == BGP_ATTR_ENCAP) {
		/* write outer T+L */
		stream_putw(s, attr->extra->encap_tunneltype);
		stream_putw(s, attrlenfield - 4);
	}

	/* write each sub-tlv */
	for(st = subtlvs; st; st = st->next) {
		if(attrtype == BGP_ATTR_ENCAP) {
			stream_putc(s, st->type);
			stream_putc(s, st->length);
		}
		stream_put(s, st->value, st->length);
	}
}

void bgp_packet_mpattr_end(struct stream *s, size_t sizep) {
	/* Set MP attribute length. Don't count the (2) bytes used to encode
     the attr length */
	stream_putw_at(s, sizep, (stream_get_endp(s) - sizep) - 2);
}

/* Make attribute packet. */
bgp_size_t bgp_packet_attribute(struct bgp *bgp, struct peer *peer, struct stream *s, struct attr *attr, struct prefix *p, afi_t afi, safi_t safi, struct peer *from, struct prefix_rd *prd, u_char *tag) {
	size_t cp;
	size_t aspath_sizep;
	struct aspath *aspath;
	int send_as4_path = 0;
	int send_as4_aggregator = 0;
	int use32bit = (CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV)) ? 1 : 0;

	if(!bgp) {
		bgp = bgp_get_default();
	}

	/* Remember current pointer. */
	cp = stream_get_endp(s);

	if(p && !(afi == AFI_IP && safi == SAFI_UNICAST)) {
		size_t mpattrlen_pos = 0;
		mpattrlen_pos = bgp_packet_mpattr_start(s, afi, safi, attr);
		bgp_packet_mpattr_prefix(s, afi, safi, p, prd, tag);
		bgp_packet_mpattr_end(s, mpattrlen_pos);
	}

	/* Origin attribute. */
	stream_putc(s, BGP_ATTR_FLAG_TRANS);
	stream_putc(s, BGP_ATTR_ORIGIN);
	stream_putc(s, 1);
	stream_putc(s, attr->origin);

	/* AS path attribute. */

	/* If remote-peer is EBGP */
	if(peer->sort == BGP_PEER_EBGP && (!CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_AS_PATH_UNCHANGED) || attr->aspath->segments == NULL) && (!CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_RSERVER_CLIENT))) {
		aspath = aspath_dup(attr->aspath);

		if(CHECK_FLAG(bgp->config, BGP_CONFIG_CONFEDERATION)) {
			/* Strip the confed info, and then stuff our path CONFED_ID
	     on the front */
			aspath = aspath_delete_confed_seq(aspath);
			aspath = aspath_add_seq(aspath, bgp->confed_id);
		} else {
			if(peer->change_local_as) {
				/* If replace-as is specified, we only use the change_local_as when
               advertising routes. */
				if(!CHECK_FLAG(peer->flags, PEER_FLAG_LOCAL_AS_REPLACE_AS)) {
					aspath = aspath_add_seq(aspath, peer->local_as);
				}
				aspath = aspath_add_seq(aspath, peer->change_local_as);
			} else {
				aspath = aspath_add_seq(aspath, peer->local_as);
			}
		}
	} else if(peer->sort == BGP_PEER_CONFED) {
		/* A confed member, so we need to do the AS_CONFED_SEQUENCE thing */
		aspath = aspath_dup(attr->aspath);
		aspath = aspath_add_confed_seq(aspath, peer->local_as);
	} else {
		aspath = attr->aspath;
	}

	/* If peer is not AS4 capable, then:
   * - send the created AS_PATH out as AS4_PATH (optional, transitive),
   *   but ensure that no AS_CONFED_SEQUENCE and AS_CONFED_SET path segment
   *   types are in it (i.e. exclude them if they are there)
   *   AND do this only if there is at least one asnum > 65535 in the path!
   * - send an AS_PATH out, but put 16Bit ASnums in it, not 32bit, and change
   *   all ASnums > 65535 to BGP_AS_TRANS
   */

	stream_putc(s, BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
	stream_putc(s, BGP_ATTR_AS_PATH);
	aspath_sizep = stream_get_endp(s);
	stream_putw(s, 0);
	stream_putw_at(s, aspath_sizep, aspath_put(s, aspath, use32bit));

	/* OLD session may need NEW_AS_PATH sent, if there are 4-byte ASNs
   * in the path
   */
	if(!use32bit && aspath_has_as4(aspath)) {
		send_as4_path = 1; /* we'll do this later, at the correct place */
	}

	/* Nexthop attribute. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP) && afi == AFI_IP && safi == SAFI_UNICAST) /* only write NH attr for unicast safi */
	{
		stream_putc(s, BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_NEXT_HOP);
		stream_putc(s, 4);
		if(safi == SAFI_MPLS_VPN) {
			if(attr->nexthop.s_addr == 0) {
				stream_put_ipv4(s, peer->nexthop.v4.s_addr);
			} else {
				stream_put_ipv4(s, attr->nexthop.s_addr);
			}
		} else {
			stream_put_ipv4(s, attr->nexthop.s_addr);
		}
	}

	/* MED attribute. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_MULTI_EXIT_DISC)) {
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL);
		stream_putc(s, BGP_ATTR_MULTI_EXIT_DISC);
		stream_putc(s, 4);
		stream_putl(s, attr->med);
	}

	/* Local preference. */
	if(peer->sort == BGP_PEER_IBGP || peer->sort == BGP_PEER_CONFED) {
		stream_putc(s, BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_LOCAL_PREF);
		stream_putc(s, 4);
		stream_putl(s, attr->local_pref);
	}

	/* Atomic aggregate. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_ATOMIC_AGGREGATE)) {
		stream_putc(s, BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_ATOMIC_AGGREGATE);
		stream_putc(s, 0);
	}

	/* Aggregator. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_AGGREGATOR)) {
		assert(attr->extra);

		/* Common to BGP_ATTR_AGGREGATOR, regardless of ASN size */
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_AGGREGATOR);

		if(use32bit) {
			/* AS4 capable peer */
			stream_putc(s, 8);
			stream_putl(s, attr->extra->aggregator_as);
		} else {
			/* 2-byte AS peer */
			stream_putc(s, 6);

			/* Is ASN representable in 2-bytes? Or must AS_TRANS be used? */
			if(attr->extra->aggregator_as > 65535) {
				stream_putw(s, BGP_AS_TRANS);

				/* we have to send AS4_AGGREGATOR, too.
               * we'll do that later in order to send attributes in ascending
               * order.
               */
				send_as4_aggregator = 1;
			} else {
				stream_putw(s, (u_int16_t) attr->extra->aggregator_as);
			}
		}
		stream_put_ipv4(s, attr->extra->aggregator_addr.s_addr);
	}

	/* Community attribute. */
	if(CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_SEND_COMMUNITY) && (attr->flag & ATTR_FLAG_BIT(BGP_ATTR_COMMUNITIES))) {
		if(attr->community->size * 4 > 255) {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
			stream_putc(s, BGP_ATTR_COMMUNITIES);
			stream_putw(s, attr->community->size * 4);
		} else {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
			stream_putc(s, BGP_ATTR_COMMUNITIES);
			stream_putc(s, attr->community->size * 4);
		}
		stream_put(s, attr->community->val, attr->community->size * 4);
	}

	/*
   * Large Community attribute.
   */
	if(attr->extra && CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_SEND_LARGE_COMMUNITY) && (attr->flag & ATTR_FLAG_BIT(BGP_ATTR_LARGE_COMMUNITIES))) {
		if(attr->extra->lcommunity->size * 12 > 255) {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
			stream_putc(s, BGP_ATTR_LARGE_COMMUNITIES);
			stream_putw(s, attr->extra->lcommunity->size * 12);
		} else {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
			stream_putc(s, BGP_ATTR_LARGE_COMMUNITIES);
			stream_putc(s, attr->extra->lcommunity->size * 12);
		}
		stream_put(s, attr->extra->lcommunity->val, attr->extra->lcommunity->size * 12);
	}

	/* Route Reflector. */
	if(peer->sort == BGP_PEER_IBGP && from && from->sort == BGP_PEER_IBGP) {
		/* Originator ID. */
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL);
		stream_putc(s, BGP_ATTR_ORIGINATOR_ID);
		stream_putc(s, 4);

		if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_ORIGINATOR_ID)) {
			stream_put_in_addr(s, &attr->extra->originator_id);
		} else {
			stream_put_in_addr(s, &from->remote_id);
		}

		/* Cluster list. */
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL);
		stream_putc(s, BGP_ATTR_CLUSTER_LIST);

		if(attr->extra && attr->extra->cluster) {
			stream_putc(s, attr->extra->cluster->length + 4);
			/* If this peer configuration's parent BGP has cluster_id. */
			if(bgp->config & BGP_CONFIG_CLUSTER_ID) {
				stream_put_in_addr(s, &bgp->cluster_id);
			} else {
				stream_put_in_addr(s, &bgp->router_id);
			}
			stream_put(s, attr->extra->cluster->list, attr->extra->cluster->length);
		} else {
			stream_putc(s, 4);
			/* If this peer configuration's parent BGP has cluster_id. */
			if(bgp->config & BGP_CONFIG_CLUSTER_ID) {
				stream_put_in_addr(s, &bgp->cluster_id);
			} else {
				stream_put_in_addr(s, &bgp->router_id);
			}
		}
	}

	/* Extended Communities attribute. */
	if(CHECK_FLAG(peer->af_flags[afi][safi], PEER_FLAG_SEND_EXT_COMMUNITY) && (attr->flag & ATTR_FLAG_BIT(BGP_ATTR_EXT_COMMUNITIES))) {
		struct attr_extra *attre = attr->extra;

		assert(attre);

		if(peer->sort == BGP_PEER_IBGP || peer->sort == BGP_PEER_CONFED) {
			if(attre->ecommunity->size * 8 > 255) {
				stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
				stream_putc(s, BGP_ATTR_EXT_COMMUNITIES);
				stream_putw(s, attre->ecommunity->size * 8);
			} else {
				stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
				stream_putc(s, BGP_ATTR_EXT_COMMUNITIES);
				stream_putc(s, attre->ecommunity->size * 8);
			}
			stream_put(s, attre->ecommunity->val, attre->ecommunity->size * 8);
		} else {
			u_int8_t *pnt;
			int tbit;
			int ecom_tr_size = 0;
			int i;

			for(i = 0; i < attre->ecommunity->size; i++) {
				pnt = attre->ecommunity->val + (i * 8);
				tbit = *pnt;

				if(CHECK_FLAG(tbit, ECOMMUNITY_FLAG_NON_TRANSITIVE)) {
					continue;
				}

				ecom_tr_size++;
			}

			if(ecom_tr_size) {
				if(ecom_tr_size * 8 > 255) {
					stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
					stream_putc(s, BGP_ATTR_EXT_COMMUNITIES);
					stream_putw(s, ecom_tr_size * 8);
				} else {
					stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
					stream_putc(s, BGP_ATTR_EXT_COMMUNITIES);
					stream_putc(s, ecom_tr_size * 8);
				}

				for(i = 0; i < attre->ecommunity->size; i++) {
					pnt = attre->ecommunity->val + (i * 8);
					tbit = *pnt;

					if(CHECK_FLAG(tbit, ECOMMUNITY_FLAG_NON_TRANSITIVE)) {
						continue;
					}

					stream_put(s, pnt, 8);
				}
			}
		}
	}

	if(send_as4_path) {
		/* If the peer is NOT As4 capable, AND */
		/* there are ASnums > 65535 in path  THEN
       * give out AS4_PATH */

		/* Get rid of all AS_CONFED_SEQUENCE and AS_CONFED_SET
       * path segments!
       * Hm, I wonder...  confederation things *should* only be at
       * the beginning of an aspath, right?  Then we should use
       * aspath_delete_confed_seq for this, because it is already
       * there! (JK)
       * Folks, talk to me: what is reasonable here!?
       */
		aspath = aspath_delete_confed_seq(aspath);

		stream_putc(s, BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTLEN);
		stream_putc(s, BGP_ATTR_AS4_PATH);
		aspath_sizep = stream_get_endp(s);
		stream_putw(s, 0);
		stream_putw_at(s, aspath_sizep, aspath_put(s, aspath, 1));
	}

	if(aspath != attr->aspath) {
		aspath_free(aspath);
	}

	if(send_as4_aggregator) {
		assert(attr->extra);

		/* send AS4_AGGREGATOR, at this place */
		/* this section of code moved here in order to ensure the correct
       * *ascending* order of attributes
       */
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_AS4_AGGREGATOR);
		stream_putc(s, 8);
		stream_putl(s, attr->extra->aggregator_as);
		stream_put_ipv4(s, attr->extra->aggregator_addr.s_addr);
	}

	if((afi == AFI_IP || afi == AFI_IP6) && (safi == SAFI_ENCAP || safi == SAFI_MPLS_VPN)) {
		/* Tunnel Encap attribute */
		bgp_packet_mpattr_tea(bgp, peer, s, attr, BGP_ATTR_ENCAP);
	}

	/* Unknown transit attribute. */
	if(attr->extra && attr->extra->transit) {
		stream_put(s, attr->extra->transit->val, attr->extra->transit->length);
	}

	/* Return total size of attribute. */
	return stream_get_endp(s) - cp;
}

size_t bgp_packet_mpunreach_start(struct stream *s, afi_t afi, safi_t safi) {
	unsigned long attrlen_pnt;

	/* Set extended bit always to encode the attribute length as 2 bytes */
	stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTLEN);
	stream_putc(s, BGP_ATTR_MP_UNREACH_NLRI);

	attrlen_pnt = stream_get_endp(s);
	stream_putw(s, 0); /* Length of this attribute. */

	stream_putw(s, afi);
	stream_putc(s, (safi == SAFI_MPLS_VPN) ? SAFI_MPLS_LABELED_VPN : safi);
	return attrlen_pnt;
}

void bgp_packet_mpunreach_prefix(struct stream *s, struct prefix *p, afi_t afi, safi_t safi, struct prefix_rd *prd, u_char *tag) {
	bgp_packet_mpattr_prefix(s, afi, safi, p, prd, tag);
}

void bgp_packet_mpunreach_end(struct stream *s, size_t attrlen_pnt) {
	bgp_packet_mpattr_end(s, attrlen_pnt);
}

/* Initialization of attribute. */
void bgp_attr_init(void) {
	aspath_init();
	attrhash_init();
	community_init();
	ecommunity_init();
	lcommunity_init();
	cluster_init();
	transit_init();
}

void bgp_attr_finish(void) {
	aspath_finish();
	attrhash_finish();
	community_finish();
	ecommunity_finish();
	lcommunity_finish();
	cluster_finish();
	transit_finish();
}

/* Make attribute packet. */
void bgp_dump_routes_attr(struct stream *s, struct attr *attr, struct prefix *prefix) {
	unsigned long cp;
	unsigned long len;
	size_t aspath_lenp;
	struct aspath *aspath;

	/* Remember current pointer. */
	cp = stream_get_endp(s);

	/* Place holder of length. */
	stream_putw(s, 0);

	/* Origin attribute. */
	stream_putc(s, BGP_ATTR_FLAG_TRANS);
	stream_putc(s, BGP_ATTR_ORIGIN);
	stream_putc(s, 1);
	stream_putc(s, attr->origin);

	aspath = attr->aspath;

	stream_putc(s, BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
	stream_putc(s, BGP_ATTR_AS_PATH);
	aspath_lenp = stream_get_endp(s);
	stream_putw(s, 0);

	stream_putw_at(s, aspath_lenp, aspath_put(s, aspath, 1));

	/* Nexthop attribute. */
	/* If it's an IPv6 prefix, don't dump the IPv4 nexthop to save space */
	if(prefix != NULL && prefix->family != AF_INET6) {
		stream_putc(s, BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_NEXT_HOP);
		stream_putc(s, 4);
		stream_put_ipv4(s, attr->nexthop.s_addr);
	}

	/* MED attribute. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_MULTI_EXIT_DISC)) {
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL);
		stream_putc(s, BGP_ATTR_MULTI_EXIT_DISC);
		stream_putc(s, 4);
		stream_putl(s, attr->med);
	}

	/* Local preference. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_LOCAL_PREF)) {
		stream_putc(s, BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_LOCAL_PREF);
		stream_putc(s, 4);
		stream_putl(s, attr->local_pref);
	}

	/* Atomic aggregate. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_ATOMIC_AGGREGATE)) {
		stream_putc(s, BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_ATOMIC_AGGREGATE);
		stream_putc(s, 0);
	}

	/* Aggregator. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_AGGREGATOR)) {
		assert(attr->extra);
		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
		stream_putc(s, BGP_ATTR_AGGREGATOR);
		stream_putc(s, 8);
		stream_putl(s, attr->extra->aggregator_as);
		stream_put_ipv4(s, attr->extra->aggregator_addr.s_addr);
	}

	/* Community attribute. */
	if(attr->flag & ATTR_FLAG_BIT(BGP_ATTR_COMMUNITIES)) {
		if(attr->community->size * 4 > 255) {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
			stream_putc(s, BGP_ATTR_COMMUNITIES);
			stream_putw(s, attr->community->size * 4);
		} else {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
			stream_putc(s, BGP_ATTR_COMMUNITIES);
			stream_putc(s, attr->community->size * 4);
		}
		stream_put(s, attr->community->val, attr->community->size * 4);
	}

	/* Large Community attribute. */
	if(attr->extra && attr->flag & ATTR_FLAG_BIT(BGP_ATTR_LARGE_COMMUNITIES)) {
		if(attr->extra->lcommunity->size * 12 > 255) {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS | BGP_ATTR_FLAG_EXTLEN);
			stream_putc(s, BGP_ATTR_COMMUNITIES);
			stream_putw(s, attr->extra->lcommunity->size * 12);
		} else {
			stream_putc(s, BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANS);
			stream_putc(s, BGP_ATTR_COMMUNITIES);
			stream_putc(s, attr->extra->lcommunity->size * 12);
		}

		stream_put(s, attr->extra->lcommunity->val, attr->extra->lcommunity->size * 12);
	}

	/* Add a MP_NLRI attribute to dump the IPv6 next hop */
	if(prefix != NULL && prefix->family == AF_INET6 && attr->extra && (attr->extra->mp_nexthop_len == 16 || attr->extra->mp_nexthop_len == 32)) {
		int sizep;
		struct attr_extra *attre = attr->extra;

		stream_putc(s, BGP_ATTR_FLAG_OPTIONAL);
		stream_putc(s, BGP_ATTR_MP_REACH_NLRI);
		sizep = stream_get_endp(s);

		/* MP header */
		stream_putc(s, 0);	      /* Marker: Attribute length. */
		stream_putw(s, AFI_IP6);      /* AFI */
		stream_putc(s, SAFI_UNICAST); /* SAFI */

		/* Next hop */
		stream_putc(s, attre->mp_nexthop_len);
		stream_put(s, &attre->mp_nexthop_global, 16);
		if(attre->mp_nexthop_len == 32) {
			stream_put(s, &attre->mp_nexthop_local, 16);
		}

		/* SNPA */
		stream_putc(s, 0);

		/* Prefix */
		stream_put_prefix(s, prefix);

		/* Set MP attribute length. */
		stream_putc_at(s, sizep, (stream_get_endp(s) - sizep) - 1);
	}

	/* Return total size of attribute. */
	len = stream_get_endp(s) - cp - 2;
	stream_putw_at(s, cp, len);
}
