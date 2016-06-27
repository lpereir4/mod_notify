/*
 * mod_notify - SMTP Notification module for ProFTPD
 * Copyright (c) 2007-2008, Thralling Penguin LLC <joe@thrallingpenguin.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */
#include "conf.h"
#include "privs.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define MOD_NOTIFY_VERSION "mod_notify/0.1"

module notify_module;

static char *substitute_variables(cmd_rec *cmd, const char *in);
static int sendmail(const char *from_name, const char *from_address, const char *to, const char *subject, const char *body);
static int callhttpservice(const char *hostname, int port, const char *resource, const char *filename);

/**
 * Send SMTP notifications for newly uploaded files
 */
MODRET notify_upload(cmd_rec *cmd) {
	char *notify = NULL;
	char *subject = "A new file has been uploaded: %f";
	char *subject_tmp;
	char *body = "A new file has been uploaded.\n\nFile: %F\nSize: %s bytes";
	char *body_tmp;
	char *from_name = "ProFTPd";
	char *from_address = "ProFTPd@domain.com";
	config_rec *c = NULL;
	
	char *http_hostname = NULL; 
	char *http_resource = NULL;
	int http_port = -1;

	c = find_config(CURRENT_CONF, CONF_PARAM, "Notify", FALSE);
	if (!c || !strlen(c->argv[0]))
		return PR_DECLINED(cmd);
	notify = (char *) c->argv[0];

	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifySubject", FALSE);
	if (c && strlen(c->argv[0]))
		subject = (char *) c->argv[0];

	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifyBody", FALSE);
	if (c && strlen(c->argv[0]))
		body = (char *) c->argv[0];

	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifyFromName", FALSE);
	if (c && strlen(c->argv[0]))
		from_name = (char *) c->argv[0];

	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifyFromAddress", FALSE);
	if (c && strlen(c->argv[0]))
		from_address = (char *) c->argv[0];
	
	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifyHttpHostname", FALSE);
	if (c && strlen(c->argv[0]))
		http_hostname = (char *) c->argv[0];
	
	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifyHttpPort", FALSE);
	if (c && strlen(c->argv[0]))
		http_port = atoi((char *) c->argv[0]);
	
	c = find_config(CURRENT_CONF, CONF_PARAM, "NotifyHttpResource", FALSE);
	if (c && strlen(c->argv[0]))
		http_resource = (char *) c->argv[0];

	pr_log_debug(DEBUG4, MOD_NOTIFY_VERSION ": Notify: %s for %s", notify, cmd->arg);

	subject_tmp = substitute_variables(cmd, subject);
	body_tmp = substitute_variables(cmd, body);

	// sendmail(from_name, from_address, notify, subject_tmp, body_tmp);
	callhttpservice(http_hostname, http_port, http_resource, cmd->arg);

	return PR_DECLINED(cmd);
}

static char *substitute_variables(cmd_rec *cmd, const char *in) {
	char *ret = pcalloc(cmd->pool, strlen(in) * 4);
	char *wherearewe = (char *) in;
	char *out = ret;
	struct stat s;

	while (*wherearewe) {
		if (*wherearewe == '%') {
			++wherearewe;
			switch (*wherearewe) {
			case 'f':
				memcpy(out, cmd->arg, strlen(cmd->arg));
				out += strlen(cmd->arg);
				break;
			case 'F':
				memcpy(out, pr_fs_getcwd(), strlen(pr_fs_getcwd()));
				out += strlen(pr_fs_getcwd());
				*out = '/';
				++out;
				memcpy(out, cmd->arg, strlen(cmd->arg));
				out += strlen(cmd->arg);
				break;
			case 's':
				pr_fsio_stat(cmd->arg, &s);
				sprintf(out, "%ld", s.st_size);
				out += strlen(out);
				break;
			case 'n':
				*out = '\n';
				++out;
				break;
			default:
				*out = '%';
				++out;
			}
			++wherearewe;
		} else {
			*out = *wherearewe;
			++out;
			++wherearewe;
		}
	}
	return ret;
}

static int read_code(int sockd) {
	char inbuf[8192 * 2];
	char buffer[4096];
	ssize_t size;
	char *pos;
	int val;

	memset(inbuf, 0, sizeof(inbuf));

	while (1) {
		size = recv(sockd, buffer, sizeof(buffer), 0);
		if (size <= 0)
			return -1;

		buffer[size] = 0;
		strcat(inbuf, buffer);

		if ((pos = strchr(inbuf, '\n')) != NULL) {
			strncpy(buffer, inbuf, (pos - inbuf));
			val = (int) atol(buffer);
			return val;
		}
	}
}

static int send_line(int handle, const char *msg) {
	ssize_t n;
	size_t len = strlen(msg);
	size_t bytes_transferred;

	for (bytes_transferred = 0;
		 bytes_transferred < len;
		 bytes_transferred += n)
	{
		n = send(handle, (char *) msg + bytes_transferred,
				len - bytes_transferred,
				0);
		if (n == 0)
			return 0;
	}

	return bytes_transferred;
}

static int callhttpservice(const char *hostname, int port, const char *resource, const char *filename) {
	struct sockaddr_in server;
	int sockd = 0;
	char buffer[1024];
	char service[10];
	char body[1024];
	char user[255];
	char path[255];
	struct addrinfo hints, *servinfo;
	int rv;
	
	// Setting service port
	snprintf(service, sizeof(service), "%d", port);

	// Creation of connection socket
	if ((sockd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION "error : [ %s ]", strerror(errno));
		return EXIT_FAILURE;
	}
	
	// Initialisation and configuration of struct sockaddr_in 
	memset(&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	// Initialisation and configuration of struc addrinfo
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	
	// Fetching of address
	if ((rv = getaddrinfo(hostname, service, &hints, &servinfo)) != 0) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION "error : [ %s ]", gai_strerror(rv));
		return EXIT_FAILURE;
	}

	server.sin_addr = ((struct sockaddr_in *) servinfo->ai_addr)->sin_addr;

	// Connection
	if (connect(sockd, &server, sizeof(struct sockaddr_in)) < 0) {
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION "error : [ %s ]", strerror(errno));
		close(sockd);
		return EXIT_FAILURE;
	}
	
	// Extraction of user login
	sscanf(filename, "/home/%s/%s", user, path);
	
	// Preparation of request
	snprintf(body, sizeof(body), "{ \"filename\" : \"%s\", \"user\" : \"%s\" }", filename, user);
	
	snprintf(buffer, sizeof(buffer), "POST %s HTTP/1.1\r\n\
Host: %s:%d\r\n\
Connection: close\r\n\
Accept: */*\r\n\
Content-Length: %d\r\n\
Content-Type: application/json\r\n\
\r\n\
%s\r\n", resource, hostname, port, strlen(body), body);
	// Sending of request
	send_line(sockd, buffer);

	// Connection closing
	close(sockd);
	
	pr_log_debug(DEBUG4, MOD_NOTIFY_VERSION ": Resource : %s / Hostname : %s / Port : %d", resource, hostname, port);

	PRIVS_RELINQUISH;
	return EXIT_SUCCESS;
}

static int sendmail(const char *from_name, const char *from_address, const char *to, const char *subject, const char *body) {
	char tmp[256];
	char date[256];
	time_t t;
	struct tm tm;
	struct sockaddr_in server;
	struct hostent *he;
	int sockd = 0;
	int smtp_code = 0;

	PRIVS_ROOT;

	memset((char *)&server, 0, sizeof(server));
	if ((sockd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: Cannot create socket: %d", errno);
		return -1;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(25);

	if ((he = gethostbyname("localhost")) == 0) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: gethostbyname failed: %d", errno);
		return -1;
	}

	server.sin_addr = *(struct in_addr *) he->h_addr_list[0];

	if (connect(sockd, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) < 0) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: connect failed: %d", errno);
		return -1;
	}

	smtp_code = read_code(sockd);
	if (smtp_code != 220) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: read banner failed: %d", smtp_code);
		return -1;
	}
	send_line(sockd, "HELO localhost\n");
	smtp_code = read_code(sockd);
	if (smtp_code != 250) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: HELO failed: %d", smtp_code);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "MAIL FROM: <%s>\n", from_address);
	send_line(sockd, tmp);
	smtp_code = read_code(sockd);
	if (smtp_code != 250) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: MAIL FROM failed: %d", smtp_code);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "RCPT TO: <%s>\n", to);
	send_line(sockd, tmp);
	smtp_code = read_code(sockd);
	if (smtp_code != 250) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: RCPT TO failed: %d", smtp_code);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "DATA\n");
	send_line(sockd, tmp);
	smtp_code = read_code(sockd);
	if (smtp_code != 354) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: DATA failed: %d", smtp_code);
		return -1;
	}

	time(&t);
	localtime_r(&t, &tm);

	strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
	snprintf(tmp, sizeof(tmp), "Date: %s\n", date);
	send_line(sockd, tmp);

	snprintf(tmp, sizeof(tmp), "From: %s <%s>\n", from_name, from_address);
	send_line(sockd, tmp);

	snprintf(tmp, sizeof(tmp), "To: %s <%s>\n", to, to);
	send_line(sockd, tmp);

	snprintf(tmp, sizeof(tmp), "Subject: %s\n", subject);
	send_line(sockd, tmp);

	snprintf(tmp, sizeof(tmp), "\n%s\n\n.\n", body);
	send_line(sockd, tmp);

	smtp_code = read_code(sockd);
	if (smtp_code != 250) {
		close(sockd);
		PRIVS_RELINQUISH;
		pr_log_pri(PR_LOG_ERR, MOD_NOTIFY_VERSION ": error: DATA send failed: %d", smtp_code);
		return -1;
	}

	snprintf(tmp, sizeof(tmp), "QUIT\n");
	send_line(sockd, tmp);

	close(sockd);

	PRIVS_RELINQUISH;

	return 0;
}

MODRET notify_set_conf_notify(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("Notify", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_subject(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifySubject", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_body(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifyBody", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_from_name(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifyFromName", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_from_address(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifyFromAddress", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_from_http_hostname(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifyHttpHostname", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_from_http_port(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifyHttpPort", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

MODRET notify_set_conf_from_http_resource(cmd_rec *cmd) {
	config_rec *c = NULL;

	CHECK_ARGS(cmd, 1);
	CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL|CONF_DIR);

	c = add_config_param_str("NotifyHttpResource", 1, (void *) cmd->argv[1]);
	c->flags |= CF_MERGEDOWN;

	return PR_HANDLED(cmd);
}

static conftable notify_conftab[] = {
	{ "Notify", notify_set_conf_notify, NULL },
	{ "NotifySubject", notify_set_conf_subject, NULL },
	{ "NotifyBody", notify_set_conf_body, NULL },
	{ "NotifyFromName", notify_set_conf_from_name, NULL },
	{ "NotifyFromAddress", notify_set_conf_from_address, NULL },
	{ "NotifyHttpHostname", notify_set_conf_from_http_hostname, NULL },
	{ "NotifyHttpPort", notify_set_conf_from_http_port, NULL },
	{ "NotifyHttpResource", notify_set_conf_from_http_resource, NULL },
	// NotifySMTPHost - default: localhost
	// NotifySMTPHelo - default: localhost or my own hostname
	{ NULL }
};

static cmdtable notify_cmdtab[] = {
	{ POST_CMD, C_STOR, G_NONE, notify_upload, TRUE, FALSE },
	{ POST_CMD, C_STOU, G_NONE, notify_upload, TRUE, FALSE },
	{ POST_CMD, C_APPE, G_NONE, notify_upload, TRUE, FALSE },
	{ 0, NULL }
};

module notify_module = {
	NULL,
	NULL,
	0x20,
	"notify",
	notify_conftab,
	notify_cmdtab,
	NULL,
	NULL,
	NULL
};
