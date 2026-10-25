/* maildrop server @alpha 0.1
 * 
 * This project is currently written by AI and 
 * will be manually refactored in the future.
 *
 * Maintainer: qjf0 <qianjunfan0@outlook.com>
 */

#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static struct {
	char version[MAX_VERSION];
	char user[MAX_USER];
	char passwd[MAX_PASS];
	char content[MAX_BODY];
} msg;

/* Spool file sequence number. */
static unsigned int seq;

/* Write n bytes to fd. */
static int write_all(int fd, const void *buf, size_t n)
{
	const char *p = buf;
	size_t done = 0;

	while (done < n) {
		ssize_t ret = write(fd, p + done, n - done);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return -1;
		done += (size_t)ret;
	}

	return 0;
}

/* Read exactly n bytes from fd. */
static int read_all(int fd, void *buf, size_t n)
{
	char *p = buf;
	size_t done = 0;

	while (done < n) {
		ssize_t ret = read(fd, p + done, n - done);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return -1;
		done += (size_t)ret;
	}

	return 0;
}

/* Send one line. */
static int send_line(int fd, const char *line)
{
	if (write_all(fd, line, strlen(line)) < 0)
		return -1;

	return write_all(fd, "\n", 1);
}

/* Set the send and receive timeouts of fd. */
static int set_timeout(int fd)
{
	struct timeval tv;

	tv.tv_sec = TIMEOUT_SEC;
	tv.tv_usec = 0;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
		return -1;

	return 0;
}

/* Return the index of name in the user table, or -1. */
static int find_user(const char *name)
{
	size_t i;

	for (i = 0; i < USER_COUNT; i++) {
		if (!strcmp(user_config[i].name, name))
			return (int)i;
	}

	return -1;
}

/* Compare two buffers in constant time; return 0 if they are equal. */
static int ct_memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a;
	const unsigned char *y = b;
	unsigned char diff = 0;

	while (n--)
		diff |= *x++ ^ *y++;

	return diff != 0;
}

/* Check msg.user and msg.passwd against the user table. */
static int verify_auth(void)
{
	const struct user *u;
	size_t len;
	int idx;

	idx = find_user(msg.user);
	if (idx < 0)
		return -1;

	u = &user_config[idx];
	len = strlen(u->passwd);
	if (strlen(msg.passwd) != len)
		return -1;

	return ct_memcmp(u->passwd, msg.passwd, len);
}

/* Return 1 if name is safe to use as part of a file name. */
static int valid_name(const char *name)
{
	size_t len = strlen(name);
	size_t i;

	if (len == 0 || len >= MAX_USER)
		return 0;

	for (i = 0; i < len; i++) {
		char c = name[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-')
			continue;
		return 0;
	}

	return 1;
}

/* Store msg.content in a new spool file. */
static int save_body(const char *dir)
{
	char path[PATH_MAX];
	size_t len = strlen(msg.content);
	int fd;
	int ret;

	if (len == 0)
		return -1;

	ret = snprintf(path, sizeof(path), "%s/%s-%lld-%u.mail", dir,
		       msg.user, (long long)time(NULL), seq++);
	if (ret < 0 || (size_t)ret >= sizeof(path))
		return -1;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
		  0600);
	if (fd < 0)
		return -1;

	if (write_all(fd, msg.content, len) < 0 || fsync(fd) < 0) {
		close(fd);
		unlink(path);
		return -1;
	}

	close(fd);
	return 0;
}

/* Create the listening socket. */
static int listen_socket(void)
{
	struct sockaddr_in sa;
	int on = 1;
	int lfd;
	int err;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0)
		return -1;

	if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		goto err;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(PORT);

	/* Loopback only. */
	if (inet_pton(AF_INET, HOST, &sa.sin_addr) != 1) {
		errno = EINVAL;
		goto err;
	}

	if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto err;
	if (listen(lfd, BACKLOG) < 0)
		goto err;

	return lfd;

err:
	err = errno;
	close(lfd);
	errno = err;
	return -1;
}

/* Accept one connection. */
static int accept_client(int lfd)
{
	int cfd;
	int err;

	cfd = accept(lfd, NULL, NULL);
	if (cfd < 0)
		return -1;

	if (set_timeout(cfd) < 0) {
		err = errno;
		close(cfd);
		errno = err;
		return -1;
	}

	return cfd;
}

/* Serve one client. */
static void handle_client(int cfd, const char *dir)
{
	if (read_all(cfd, &msg, sizeof(msg)) < 0) {
		send_line(cfd, "ERR proto");
		return;
	}

	/* Terminate both strings. */
	msg.version[sizeof(msg.version) - 1] = '\0';
	msg.content[sizeof(msg.content) - 1] = '\0';

	/* Refuse a different protocol version. */
	if (strcmp(msg.version, VERSION)) {
		char line[MAX_LINE];

		/* Report the required version. */
		snprintf(line, sizeof(line), "ERR version server=%s", VERSION);
		send_line(cfd, line);
		return;
	}

	if (verify_auth() < 0) {
		sleep(1);
		send_line(cfd, "ERR auth");
		return;
	}

	if (!valid_name(msg.user)) {
		send_line(cfd, "ERR user");
		return;
	}

	if (save_body(dir) < 0) {
		send_line(cfd, "ERR io");
		return;
	}

	send_line(cfd, "OK");
}

int main(int argc, char **argv)
{
	struct stat st;
	const char *dir;
	int lfd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <spool-dir>\n", argv[0]);
		return EXIT_FAILURE;
	}
	dir = argv[1];

	if (stat(dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
		fprintf(stderr, "%s: not a directory\n", dir);
		return EXIT_FAILURE;
	}

	if (USER_COUNT == 0) {
		fprintf(stderr, "config.h: empty user table\n");
		return EXIT_FAILURE;
	}

	signal(SIGPIPE, SIG_IGN);

	lfd = listen_socket();
	if (lfd < 0) {
		perror("listen");
		return EXIT_FAILURE;
	}

	for (;;) {
		int cfd = accept_client(lfd);

		if (cfd < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			continue;
		}

		handle_client(cfd, dir);
		close(cfd);
	}
}
