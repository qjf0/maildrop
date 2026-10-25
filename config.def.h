/* SPDX-License-Identifier: MIT */
/*
 * maildrop - compile time configuration
 *
 * The user table is compiled in, suckless style.  To change a password
 * edit user_config[] and rebuild; there is no runtime reload.
 */

#ifndef MAILDROP_CONFIG_H
#define MAILDROP_CONFIG_H

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))

#define HOST		"127.0.0.1"
#define PORT		9000
#define BACKLOG		16
#define TIMEOUT_SEC	30

/* Must match between client and server. */
#define VERSION		"maildrop/1"

#define MAX_VERSION	32
#define MAX_USER	32
#define MAX_PASS	32
#define MAX_BODY	(512 * 1024)
#define MAX_LINE	256
#define MAX_REPLY	64

struct user {
	char name[MAX_USER];
	char passwd[MAX_PASS];
};

static const struct user user_config[] = {
	{ "user",  "password" },
	{ "user2", "password" },
};

#define USER_COUNT	ARRAY_SIZE(user_config)

#endif /* MAILDROP_CONFIG_H */
