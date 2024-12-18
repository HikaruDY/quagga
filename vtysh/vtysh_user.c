/* User authentication for vtysh.
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

#include <zebra.h>
#include <lib/version.h>

#include <pwd.h>

#ifdef USE_PAM
	#include <security/pam_appl.h>
	#ifdef HAVE_PAM_MISC_H
		#include <security/pam_misc.h>
	#endif
	#ifdef HAVE_OPENPAM_H
		#include <security/openpam.h>
	#endif
#endif /* USE_PAM */

#include "memory.h"
#include "linklist.h"
#include "command.h"
#include "vtysh_user.h"

#ifdef USE_PAM
static struct pam_conv conv = { PAM_CONV_FUNC, NULL };

static int vtysh_pam(const char *user) {
	int ret;
	pam_handle_t *pamh = NULL;

	/* Start PAM. */
	ret = pam_start(QUAGGA_PROGNAME, user, &conv, &pamh);
	/* printf ("ret %d\n", ret); */

	/* Is user really user? */
	if(ret == PAM_SUCCESS) {
		ret = pam_authenticate(pamh, 0);
	}
	if(ret != PAM_SUCCESS) {
		printf("Not authenticated. Check /etc/pam.d/quagga.\n");
	}
	/* printf ("ret %d\n", ret); */

	#if 0
  /* Permitted access? */
  if (ret == PAM_SUCCESS)
    ret = pam_acct_mgmt (pamh, 0);
  printf ("ret %d\n", ret);

  if (ret == PAM_AUTHINFO_UNAVAIL)
    ret = PAM_SUCCESS;
	#endif /* 0 */

	/* This is where we have been authorized or not. */
	#ifdef DEBUG
	if(ret == PAM_SUCCESS) {
		printf("Authenticated\n");
	} else {
		printf("Not Authenticated\n");
	}
	#endif /* DEBUG */

	/* close Linux-PAM */
	if(pam_end(pamh, ret) != PAM_SUCCESS) {
		pamh = NULL;
		fprintf(stderr, "vtysh_pam: failed to release authenticator\n");
		exit(1);
	}

	return ret == PAM_SUCCESS ? 0 : 1;
}
#endif /* USE_PAM */

struct vtysh_user {
	char *name;
	u_char nopassword;
};

struct list *userlist;

static struct vtysh_user *user_new() {
	return XCALLOC(MTYPE_TMP, sizeof(struct vtysh_user));
}

#if 0
static void
user_free (struct vtysh_user *user)
{
  XFREE (0, user);
}
#endif

static struct vtysh_user *user_lookup(const char *name) {
	struct listnode *node, *nnode;
	struct vtysh_user *user;

	for(ALL_LIST_ELEMENTS(userlist, node, nnode, user)) {
		if(strcmp(user->name, name) == 0) {
			return user;
		}
	}
	return NULL;
}

#if 0
static void
user_config_write ()
{
  struct listnode *node, *nnode;
  struct vtysh_user *user;

  for (ALL_LIST_ELEMENTS (userlist, node, nnode, user))
    {
      if (user->nopassword)
	printf (" username %s nopassword\n", user->name);
    }
}
#endif

static struct vtysh_user *user_get(const char *name) {
	struct vtysh_user *user;
	user = user_lookup(name);
	if(user) {
		return user;
	}

	user = user_new();
	user->name = strdup(name);
	listnode_add(userlist, user);

	return user;
}

DEFUN(username_nopassword, username_nopassword_cmd, "username WORD nopassword",
      "\n"
      "\n"
      "\n") {
	struct vtysh_user *user;
	user = user_get(argv[0]);
	user->nopassword = 1;
	return CMD_SUCCESS;
}

int vtysh_auth(void) {
	struct vtysh_user *user;
	struct passwd *passwd;

	if((passwd = getpwuid(geteuid())) == NULL) {
		fprintf(stderr, "could not lookup user ID %d\n", (int) geteuid());
		exit(1);
	}

	user = user_lookup(passwd->pw_name);
	if(user && user->nopassword) /* Pass through */
		;
	else {
#ifdef USE_PAM
		if(vtysh_pam(passwd->pw_name)) {
			exit(0);
		}
#endif /* USE_PAM */
	}
	return 0;
}

char *vtysh_get_home(void) {
	struct passwd *passwd;

	passwd = getpwuid(getuid());

	return passwd ? passwd->pw_dir : NULL;
}

void vtysh_user_init(void) {
	userlist = list_new();
	install_element(CONFIG_NODE, &username_nopassword_cmd);
}
