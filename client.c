/* maildrop client @alpha 0.1
 *
 * This project is currently written by AI and 
 * will be manually refactored in the future.
 *
 * Maintainer: qjf0 <qianjunfan0@outlook.com>
 */

#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"

static struct {
	char version[MAX_VERSION];
	char user[MAX_USER];
	char passwd[MAX_PASS];
	char content[MAX_BODY];
} msg;

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

/* Read one line from fd. */
static int recv_line(int fd, char *buf, size_t cap)
{
	size_t i = 0;

	if (cap == 0)
		return -1;

	while (i + 1 < cap) {
		char c;
		ssize_t ret = read(fd, &c, 1);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return -1;
		if (c == '\n') {
			buf[i] = '\0';
			return 0;
		}
		buf[i++] = c;
	}

	return -1;
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

/* Load path into msg.content. */
static int read_file(const char *path)
{
	char *line = NULL;
	size_t cap = 0;
	size_t used = 0;
	ssize_t n;
	FILE *fp;

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		return -1;
	}

	while ((n = getline(&line, &cap, fp)) > 0) {
		/* A NUL byte means this is not a text file. */
		if (memchr(line, '\0', (size_t)n)) {
			fprintf(stderr, "%s: not a text file\n", path);
			goto err;
		}

		if (used + (size_t)n + 1 > sizeof(msg.content)) {
			fprintf(stderr, "%s: file too large\n", path);
			goto err;
		}

		memcpy(msg.content + used, line, (size_t)n);
		used += (size_t)n;
	}

	if (ferror(fp)) {
		fprintf(stderr, "%s: read error\n", path);
		goto err;
	}

	free(line);
	fclose(fp);

	if (used == 0) {
		fprintf(stderr, "%s: empty file\n", path);
		return -1;
	}

	msg.content[used] = '\0';
	return 0;

err:
	free(line);
	fclose(fp);
	return -1;
}

/* Discard the rest of the current input line. */
static void drain_line(void)
{
	int c;

	while ((c = getchar()) != '\n' && c != EOF)
		;
}

/* Read msg.user from stdin. */
static int read_username(void)
{
	char buf[MAX_LINE];
	size_t len;
	int tries;

	for (tries = 0; tries < 3; tries++) {
		printf("username: ");
		fflush(stdout);

		if (!fgets(buf, sizeof(buf), stdin))
			return -1;

		/* Drop an over long line. */
		if (!strchr(buf, '\n') && !feof(stdin)) {
			drain_line();
			continue;
		}

		buf[strcspn(buf, "\n")] = '\0';
		len = strlen(buf);
		if (len == 0 || len >= sizeof(msg.user))
			continue;

		memcpy(msg.user, buf, len + 1);
		return 0;
	}

	return -1;
}

/* Read msg.passwd from stdin without echo. */
static int read_passwd(char *buf, size_t cap)
{
	struct termios saved;
	struct termios noecho;
	size_t len = 0;
	int restore = 0;
	int overflow = 0;
	int c;

	if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
		noecho = saved;
		noecho.c_lflag &= ~(ECHO | ICANON);
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &noecho) == 0)
			restore = 1;
	}

	fputs("password: ", stdout);
	fflush(stdout);

	while ((c = getchar()) != EOF && c != '\n') {
		if (c == 127 || c == '\b') {
			if (len > 0)
				len--;
			continue;
		}
		if (len + 1 < cap)
			buf[len++] = (char)c;
		else
			overflow = 1;
	}
	buf[len] = '\0';

	if (restore) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
		fputc('\n', stdout);
	}

	if (overflow || len == 0)
		return -1;

	return 0;
}

/* Read msg.user and msg.passwd. */
static int input_info(void)
{
	if (read_username() < 0)
		return -1;

	return read_passwd(msg.passwd, sizeof(msg.passwd));
}

/* Connect to host:port. */
static int connect_server(const char *host, int port)
{
	struct sockaddr_in sa;
	int fd;
	int err;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
		errno = EINVAL;
		goto err;
	}

	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto err;

	if (set_timeout(fd) < 0)
		goto err;

	return fd;

err:
	err = errno;
	close(fd);
	errno = err;
	return -1;
}

/* Report a version mismatch and the version the server requires. */
static void report_version(const char *reply)
{
	char server[MAX_VERSION] = "?";
	const char *p;
	size_t len;

	p = strstr(reply, "server=");
	if (p) {
		p += strlen("server=");
		len = strcspn(p, " ");
		if (len >= sizeof(server))
			len = sizeof(server) - 1;
		memcpy(server, p, len);
		server[len] = '\0';
	}

	fprintf(stderr, "version mismatch: client %s, server %s\n", VERSION,
		server);

	if (!strcmp(server, "?"))
		fprintf(stderr, "please update the client and try again\n");
	else
		fprintf(stderr, "please update the client to %s and try again\n",
			server);
}

int main(int argc, char **argv)
{
	char reply[MAX_REPLY];
	int fd;
	int ret = EXIT_FAILURE;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (read_file(argv[1]) < 0)
		return EXIT_FAILURE;

	if (input_info() < 0) {
		fprintf(stderr, "invalid user name or password\n");
		return EXIT_FAILURE;
	}

	fd = connect_server(HOST, PORT);
	if (fd < 0) {
		perror("connect");
		return EXIT_FAILURE;
	}

	snprintf(msg.version, sizeof(msg.version), "%s", VERSION);

	if (write_all(fd, &msg, sizeof(msg)) < 0) {
		perror("send");
		goto out;
	}

	if (recv_line(fd, reply, sizeof(reply)) < 0) {
		fprintf(stderr, "no reply from server\n");
		goto out;
	}

	if (!strcmp(reply, "OK")) {
		printf("sent\n");
		ret = EXIT_SUCCESS;
	} else if (!strncmp(reply, "ERR version", 11)) {
		report_version(reply);
	} else if (!strcmp(reply, "ERR auth")) {
		fprintf(stderr, "authentication failed\n");
	} else {
		fprintf(stderr, "server replied: %s\n", reply);
	}

out:
	close(fd);
	return ret;
}
