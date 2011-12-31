/*
 * Copyright (C) 2010 Martin Willi
 * Copyright (C) 2010 revosec AG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>

#include <library.h>
#include <debug.h>
#include <tls_socket.h>
#include <utils/host.h>
#include <credentials/sets/mem_cred.h>

/**
 * Print usage information
 */
static void usage(FILE *out, char *cmd)
{
	fprintf(out, "usage:\n");
	fprintf(out, "  %s --connect <address> --port <port> [--cert <file>]+ [--times <n>]\n", cmd);
	fprintf(out, "  %s --listen <address> --port <port> --key <key> [--cert <file>]+ [--times <n>]\n", cmd);
}

/**
 * Stream between stdio and TLS socket
 */
static int stream(int fd, tls_socket_t *tls)
{
	while (TRUE)
	{
		fd_set set;
		chunk_t data;

		FD_ZERO(&set);
		FD_SET(fd, &set);
		FD_SET(0, &set);

		if (select(fd + 1, &set, NULL, NULL, NULL) == -1)
		{
			return 1;
		}
		if (FD_ISSET(fd, &set))
		{
			if (!tls->read(tls, &data))
			{
				return 0;
			}
			if (data.len)
			{
				ignore_result(write(1, data.ptr, data.len));
				free(data.ptr);
			}
		}
		if (FD_ISSET(0, &set))
		{
			char buf[1024];
			ssize_t len;

			len = read(0, buf, sizeof(buf));
			if (len == 0)
			{
				return 0;
			}
			if (len > 0)
			{
				if (!tls->write(tls, chunk_create(buf, len)))
				{
					DBG1(DBG_TLS, "TLS write error");
					return 1;
				}
			}
		}
	}
}

/**
 * Client routine
 */
static int client(host_t *host, identification_t *server,
				  int times, tls_cache_t *cache)
{
	tls_socket_t *tls;
	int fd, res;

	while (times == -1 || times-- > 0)
	{
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd == -1)
		{
			DBG1(DBG_TLS, "opening socket failed: %s", strerror(errno));
			return 1;
		}
		if (connect(fd, host->get_sockaddr(host),
					*host->get_sockaddr_len(host)) == -1)
		{
			DBG1(DBG_TLS, "connecting to %#H failed: %s", host, strerror(errno));
			close(fd);
			return 1;
		}
		tls = tls_socket_create(FALSE, server, NULL, fd, cache);
		if (!tls)
		{
			close(fd);
			return 1;
		}
		res = stream(fd, tls);
		tls->destroy(tls);
		close(fd);
		if (res)
		{
			break;
		}
	}
	return res;
}

/**
 * Server routine
 */
static int serve(host_t *host, identification_t *server,
				 int times, tls_cache_t *cache)
{
	tls_socket_t *tls;
	int fd, cfd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
	{
		DBG1(DBG_TLS, "opening socket failed: %s", strerror(errno));
		return 1;
	}
	if (bind(fd, host->get_sockaddr(host),
			 *host->get_sockaddr_len(host)) == -1)
	{
		DBG1(DBG_TLS, "binding to %#H failed: %s", host, strerror(errno));
		close(fd);
		return 1;
	}
	if (listen(fd, 1) == -1)
	{
		DBG1(DBG_TLS, "listen to %#H failed: %m", host, strerror(errno));
		close(fd);
		return 1;
	}

	while (times == -1 || times-- > 0)
	{
		cfd = accept(fd, host->get_sockaddr(host), host->get_sockaddr_len(host));
		if (cfd == -1)
		{
			DBG1(DBG_TLS, "accept failed: %s", strerror(errno));
			close(fd);
			return 1;
		}
		DBG1(DBG_TLS, "%#H connected", host);

		tls = tls_socket_create(TRUE, server, NULL, cfd, cache);
		if (!tls)
		{
			close(fd);
			return 1;
		}
		stream(cfd, tls);
		DBG1(DBG_TLS, "%#H disconnected", host);
		tls->destroy(tls);
	}
	close(fd);

	return 0;
}

/**
 * In-Memory credential set
 */
static mem_cred_t *creds;

/**
 * Load certificate from file
 */
static bool load_certificate(char *filename)
{
	certificate_t *cert;

	cert = lib->creds->create(lib->creds, CRED_CERTIFICATE, CERT_X509,
							  BUILD_FROM_FILE, filename, BUILD_END);
	if (!cert)
	{
		DBG1(DBG_TLS, "loading certificate from '%s' failed", filename);
		return FALSE;
	}
	creds->add_cert(creds, TRUE, cert);
	return TRUE;
}

/**
 * Load private key from file
 */
static bool load_key(char *filename)
{
	private_key_t *key;

	key = lib->creds->create(lib->creds, CRED_PRIVATE_KEY, KEY_RSA,
							  BUILD_FROM_FILE, filename, BUILD_END);
	if (!key)
	{
		DBG1(DBG_TLS, "loading key from '%s' failed", filename);
		return FALSE;
	}
	creds->add_key(creds, key);
	return TRUE;
}

/**
 * TLS debug level
 */
static level_t tls_level = 1;

static void dbg_tls(debug_t group, level_t level, char *fmt, ...)
{
	if ((group == DBG_TLS && level <= tls_level) || level <= 1)
	{
		va_list args;

		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		fprintf(stderr, "\n");
		va_end(args);
	}
}

/**
 * Cleanup
 */
static void cleanup()
{
	lib->credmgr->remove_set(lib->credmgr, &creds->set);
	creds->destroy(creds);
	library_deinit();
}

/**
 * Initialize library
 */
static void init()
{
	library_init(NULL);

	dbg = dbg_tls;

	lib->plugins->load(lib->plugins, NULL, PLUGINS);

	creds = mem_cred_create();
	lib->credmgr->add_set(lib->credmgr, &creds->set);

	atexit(cleanup);
}

int main(int argc, char *argv[])
{
	char *address = NULL;
	bool listen = FALSE;
	int port = 0, times = -1, res;
	identification_t *server;
	tls_cache_t *cache;
	host_t *host;

	init();

	while (TRUE)
	{
		struct option long_opts[] = {
			{"help",		no_argument,			NULL,		'h' },
			{"connect",		required_argument,		NULL,		'c' },
			{"listen",		required_argument,		NULL,		'l' },
			{"port",		required_argument,		NULL,		'p' },
			{"cert",		required_argument,		NULL,		'x' },
			{"key",			required_argument,		NULL,		'k' },
			{"times",		required_argument,		NULL,		't' },
			{"debug",		required_argument,		NULL,		'd' },
			{0,0,0,0 }
		};
		switch (getopt_long(argc, argv, "", long_opts, NULL))
		{
			case EOF:
				break;
			case 'h':
				usage(stdout, argv[0]);
				return 0;
			case 'x':
				if (!load_certificate(optarg))
				{
					return 1;
				}
				continue;
			case 'k':
				if (!load_key(optarg))
				{
					return 1;
				}
				continue;
			case 'l':
				listen = TRUE;
				/* fall */
			case 'c':
				if (address)
				{
					usage(stderr, argv[0]);
					return 1;
				}
				address = optarg;
				continue;
			case 'p':
				port = atoi(optarg);
				continue;
			case 't':
				times = atoi(optarg);
				continue;
			case 'd':
				tls_level = atoi(optarg);
				continue;
			default:
				usage(stderr, argv[0]);
				return 1;
		}
		break;
	}
	if (!port || !address)
	{
		usage(stderr, argv[0]);
		return 1;
	}
	host = host_create_from_dns(address, 0, port);
	if (!host)
	{
		DBG1(DBG_TLS, "resolving hostname %s failed", address);
		return 1;
	}
	server = identification_create_from_string(address);
	cache = tls_cache_create(100, 30);
	if (listen)
	{
		res = serve(host, server, times, cache);
	}
	else
	{
		res = client(host, server, times, cache);
	}
	cache->destroy(cache);
	host->destroy(host);
	server->destroy(server);
	return res;
}

