/* socklib.c
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#if WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#endif

#if __UNIX__
#include <arpa/inet.h>
#elif __BEOS__
#include <be/net/netdb.h>   
#endif

#include "srtypes.h"
#include "socklib.h"
#include "threadlib.h"
#include "sr_compat.h"
#include "debug.h"
#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifdef WIN32
#include <wincrypt.h>
#endif
#endif


#if WIN32
#define DEFAULT_TIMEOUT		(15 * 1000)
#define FIRST_READ_TIMEOUT	(30 * 1000)
#endif


/****************************************************************************
 * Function definitions
 ****************************************************************************/
error_code
socklib_init()
{
#if WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD( 2, 2 );
    err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 )
        return SR_ERROR_WIN32_INIT_FAILURE;
#endif

    return SR_SUCCESS;
}

/* Try to find the local interface to bind to */
error_code
read_interface (char *if_name, uint32_t *addr)
{
#if defined (WIN32)
    return -1;
#else
    int fd;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(struct ifreq));
    if((fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	ifr.ifr_addr.sa_family = AF_INET;
	g_strlcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFADDR, &ifr) == 0)
	    *addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
	else {
	    close(fd);
	    return -2;
	}
    } else 
	return -1;
    close(fd);
    return 0;
#endif
}

/*
 * open's a tcp connection to host at port, host can be a dns name or IP,
 * socket_handle gets assigned to the handle for the connection
 */
error_code
socklib_bind_interface(SOCKET sock, int family, const char *if_name)
{
#ifndef WIN32
    struct ifaddrs *interfaces, *entry;
    error_code result = SR_ERROR_CANT_BIND_ON_INTERFACE;
    if (!if_name || !*if_name) return SR_SUCCESS;
    if (getifaddrs(&interfaces) != 0) return result;
    for (entry = interfaces; entry; entry = entry->ifa_next) {
        if (entry->ifa_addr && entry->ifa_addr->sa_family == family &&
            !strcmp(entry->ifa_name, if_name)) {
            socklen_t length = family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
            if (bind(sock, entry->ifa_addr, length) == 0) {
                result = SR_SUCCESS;
                break;
            }
        }
    }
    freeifaddrs(interfaces);
    return result;
#else
    /* The inherited -I setting is Unix-only. */
    return SR_SUCCESS;
#endif
}

static int
set_socket_blocking(SOCKET sock, int blocking)
{
#ifdef WIN32
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(sock, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
#endif
}

error_code
socklib_open (HSOCKET *socket_handle, char *host, int port,
              char *if_name, int timeout, int ip_version)
{
    struct addrinfo hints = {0}, *addresses = NULL, *address;
    char service[6];
    GArray *pending;
    SOCKET connected = (SOCKET)SOCKET_ERROR;
    gint64 deadline, next_attempt = 0;
    error_code result = SR_ERROR_CONNECT_FAILED;

    if (!socket_handle || !host || port < 1 || port > 65535 ||
        (ip_version != 0 && ip_version != 4 && ip_version != 6))
        return SR_ERROR_INVALID_PARAM;
    socket_handle->closed = TRUE;
    socket_handle->s = SOCKET_ERROR;
    socket_handle->chunked = FALSE;
    socket_handle->chunk_done = FALSE;
    socket_handle->chunk_crlf = FALSE;
    socket_handle->chunk_remaining = 0;
#ifdef HAVE_OPENSSL
    socket_handle->tls = NULL;
    socket_handle->proxy_tls = NULL;
#endif
    hints.ai_family = ip_version == 4 ? AF_INET : ip_version == 6 ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(service, sizeof(service), "%d", port);
    /* Do not use AI_ADDRCONFIG: loopback needs no globally configured address. */
    if (getaddrinfo(host, service, &hints, &addresses) != 0)
        return SR_ERROR_CANT_RESOLVE_HOSTNAME;
    pending = g_array_new(FALSE, FALSE, sizeof(SOCKET));
    address = addresses;
    deadline = g_get_monotonic_time() + (gint64)(timeout > 0 ? timeout : 15) * G_USEC_PER_SEC;

    /* Stagger address attempts by 250 ms; a slow IPv6 route must not block IPv4.
       All attempts share one timeout, and every losing socket is closed. */
    while (address || pending->len) {
        fd_set writable, failed;
        struct timeval wait;
        gint64 now = g_get_monotonic_time(), until;
        SOCKET highest = 0;
        int selected;
        guint i;
        if (now >= deadline) break;
        if (address && now >= next_attempt && pending->len < FD_SETSIZE) {
            struct addrinfo *candidate = address;
            SOCKET sock;
            int rc, in_progress;
            address = address->ai_next;
            sock = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
            if (sock == (SOCKET)SOCKET_ERROR) {
                result = SR_ERROR_CANT_CREATE_SOCKET;
                continue;
            }
#ifndef WIN32
            if (sock >= FD_SETSIZE) {
                closesocket(sock);
                continue;
            }
#endif
            result = socklib_bind_interface(sock, candidate->ai_family, if_name);
            if (result == SR_SUCCESS) {
#ifdef WIN32
                DWORD interval = (DWORD)(timeout > 0 ? timeout : 15) * 1000;
#else
                struct timeval interval = {timeout > 0 ? timeout : 15, 0};
#endif
                if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&interval, sizeof(interval)) != 0 ||
                    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&interval, sizeof(interval)) != 0)
                    result = SR_ERROR_CANT_SET_SOCKET_OPTIONS;
                else if (set_socket_blocking(sock, FALSE) != 0)
                    result = SR_ERROR_CONNECT_FAILED;
            }
            if (result != SR_SUCCESS) {
                closesocket(sock);
                continue;
            }
            rc = connect(sock, candidate->ai_addr, (socklen_t)candidate->ai_addrlen);
            if (rc == 0) {
                connected = sock;
                break;
            }
#ifdef WIN32
            rc = WSAGetLastError();
            in_progress = rc == WSAEWOULDBLOCK || rc == WSAEINPROGRESS;
#else
            in_progress = errno == EINPROGRESS;
#endif
            result = SR_ERROR_CONNECT_FAILED;
            if (!in_progress) {
                closesocket(sock);
                continue;
            }
            g_array_append_val(pending, sock);
            next_attempt = now + 250000;
        }
        if (!pending->len) {
            next_attempt = 0;
            continue;
        }
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        for (i = 0; i < pending->len; i++) {
            SOCKET sock = g_array_index(pending, SOCKET, i);
            FD_SET(sock, &writable);
            FD_SET(sock, &failed);
            if (sock > highest) highest = sock;
        }
        until = deadline;
        if (address && pending->len < FD_SETSIZE && next_attempt < until) until = next_attempt;
        until = MAX((gint64)0, until - g_get_monotonic_time());
        wait.tv_sec = (long)(until / G_USEC_PER_SEC);
        wait.tv_usec = (long)(until % G_USEC_PER_SEC);
#ifdef WIN32
        selected = select(0, NULL, &writable, &failed, &wait);
#else
        selected = select(highest + 1, NULL, &writable, &failed, &wait);
#endif
        if (selected < 0) {
#ifdef WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            result = SR_ERROR_CONNECT_FAILED;
            break;
        }
        for (i = pending->len; i > 0; i--) {
            SOCKET sock = g_array_index(pending, SOCKET, i - 1);
            if (FD_ISSET(sock, &writable) || FD_ISSET(sock, &failed)) {
                int error = 0;
                socklen_t length = sizeof(error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&error, &length) == 0 && !error) {
                    connected = sock;
                    break;
                }
                closesocket(sock);
                g_array_remove_index_fast(pending, i - 1);
            }
        }
        if (connected != (SOCKET)SOCKET_ERROR) break;
        if (!pending->len) next_attempt = 0;
    }
    for (guint i = 0; i < pending->len; i++) {
        SOCKET sock = g_array_index(pending, SOCKET, i);
        if (sock != connected) closesocket(sock);
    }
    g_array_free(pending, TRUE);
    freeaddrinfo(addresses);
    if (connected != (SOCKET)SOCKET_ERROR) {
        if (set_socket_blocking(connected, TRUE) != 0) {
            closesocket(connected);
            return SR_ERROR_CONNECT_FAILED;
        }
        socket_handle->s = connected;
        socket_handle->closed = FALSE;
        return SR_SUCCESS;
    }
    return result;
}

#ifdef HAVE_OPENSSL
/* TLS state is released by the reader after it stops, never by the thread
   which closes the socket to interrupt a blocking read. */
void
socklib_free_tls (HSOCKET *socket_handle)
{
    if (socket_handle->tls) {
        SSL_free((SSL *)socket_handle->tls);
        socket_handle->tls = NULL;
    }
    if (socket_handle->proxy_tls) {
        SSL_free((SSL *)socket_handle->proxy_tls);
        socket_handle->proxy_tls = NULL;
    }
}

error_code
socklib_start_tls (HSOCKET *socket_handle, const char *host, const STREAM_PREFS *prefs, BOOL proxy)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL *ssl;
    SSL *transport = (SSL *)socket_handle->tls;
    unsigned char ip[16];
    char verification_host[MAX_HOST_LEN];
    char *zone;
    if (!ctx) return SR_ERROR_CONNECT_FAILED;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, prefs->tls_insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_default_verify_paths(ctx);
    if (prefs->tls_ca_file[0] && !SSL_CTX_load_verify_locations(ctx, prefs->tls_ca_file, NULL)) {
        fprintf(stderr, "Unable to load CA file: %s\n", prefs->tls_ca_file);
        SSL_CTX_free(ctx);
        return SR_ERROR_CONNECT_FAILED;
    }
    if (prefs->tls_insecure) fprintf(stderr, "Warning: --insecure disables certificate and hostname verification.\n");
#ifdef WIN32
    {
        HCERTSTORE roots = CertOpenSystemStoreA(0, "ROOT");
        PCCERT_CONTEXT cert = NULL;
        if (roots) {
            while ((cert = CertEnumCertificatesInStore(roots, cert)) != NULL) {
                const unsigned char *der = cert->pbCertEncoded;
                X509 *x509 = d2i_X509(NULL, &der, cert->cbCertEncoded);
                if (x509) {
                    X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), x509);
                    X509_free(x509);
                }
            }
            CertCloseStore(roots, 0);
        }
    }
#endif
    ssl = SSL_new(ctx);
    SSL_CTX_free(ctx);
    if (!ssl) return SR_ERROR_CONNECT_FAILED;
    if (transport) {
        BIO *tunnel = BIO_new(BIO_f_ssl());
        if (!tunnel) {
            SSL_free(ssl);
            return SR_ERROR_CONNECT_FAILED;
        }
        /* The inner SSL owns its filter BIO, while we retain the outer SSL.
           Free the inner object first, then release proxy_tls explicitly. */
        BIO_set_ssl(tunnel, transport, BIO_NOCLOSE);
        SSL_set_bio(ssl, tunnel, tunnel);
        socket_handle->proxy_tls = transport;
    }
    socket_handle->tls = ssl;
    ERR_clear_error();
    /* Scope IDs route locally; certificate IP identities never include them. */
    g_strlcpy(verification_host, host, sizeof(verification_host));
    if (strchr(verification_host, ':') && (zone = strchr(verification_host, '%')) != NULL) *zone = 0;
    if (inet_pton(AF_INET, verification_host, ip) == 1 || inet_pton(AF_INET6, verification_host, ip) == 1) {
        if (!X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), verification_host)) goto failed;
    } else {
        if (!SSL_set_tlsext_host_name(ssl, host) || !SSL_set1_host(ssl, host)) goto failed;
    }
    if ((!transport && !SSL_set_fd(ssl, (int)socket_handle->s)) || SSL_connect(ssl) != 1) goto failed;
    debug_printf("TLS connected: %s, %s\n", SSL_get_version(ssl), SSL_get_cipher(ssl));
    if (prefs->tls_info) {
        const char *signature = "unknown";
        const char *group = SSL_group_to_name(ssl, SSL_get_negotiated_group(ssl));
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
        SSL_get0_peer_signature_name(ssl, &signature);
#else
        int nid;
        if (SSL_get_peer_signature_type_nid(ssl, &nid)) signature = OBJ_nid2sn(nid);
#endif
        fprintf(stdout, "TLS%s: %s; cipher: %s; group: %s; signature: %s; verification: %s\n",
                proxy ? " proxy" : "", SSL_get_version(ssl), SSL_get_cipher(ssl), group ? group : "unknown",
                signature ? signature : "unknown", prefs->tls_insecure ? "disabled" : "enabled");
        fflush(stdout);
    }
    return SR_SUCCESS;
failed:
    fprintf(stderr, "TLS%s error for %s: %s (%s)\n", proxy ? " proxy" : "", host,
            ERR_peek_last_error() ? ERR_reason_error_string(ERR_peek_last_error()) : "connection closed",
            X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
    socklib_free_tls(socket_handle);
    return SR_ERROR_CONNECT_FAILED;
}
#endif

void
socklib_cleanup()
{
    WSACleanup();
}

void
socklib_close(HSOCKET *socket_handle)
{
    if (!socket_handle->closed) closesocket(socket_handle->s);
    socket_handle->closed = TRUE;
}

error_code
socklib_read_header(RIP_MANAGER_INFO* rmi, HSOCKET *socket_handle, 
		    char *buffer, int size)
{
    int i;
#ifdef WIN32
    int timeout;
#endif
    int ret;
    char *t;

    if (socket_handle->closed)
	return SR_ERROR_SOCKET_CLOSED;

#ifdef WIN32
    timeout = 2 * rmi->prefs->timeout * 1000;  /* Convert sec to msec */
    if (setsockopt (socket_handle->s, SOL_SOCKET,  SO_RCVTIMEO, (char *)&timeout, sizeof(int)) == SOCKET_ERROR)
	return SR_ERROR_CANT_SET_SOCKET_OPTIONS;
#endif

    memset(buffer, 0, size);
    for (i = 0; i < size; i++)
    {
	ret = socklib_recvall (rmi, socket_handle, &buffer[i], 1, 0);
	if (ret < 0) {
	    return ret;
	}

	if (ret == 0) {
	    debug_printf("http header:\n%s\n", buffer);
	    return SR_ERROR_NO_HTTP_HEADER;
	}

	if (socket_handle->closed)
	    return SR_ERROR_SOCKET_CLOSED;

#if defined (commentout)
	/* This is too restrictive.  Some servers do not send this. */
	if (!strstr(buffer, "icy-") && !strstr(buffer,"ice-"))
	    continue;
#endif

	t = buffer + (i > 3 ? i - 3: i);

	if (strncmp(t, "\r\n\r\n", 4) == 0)
	    break;
	/* Allegedly live365 used to do this */
	if (strncmp(t, "\n\0\r\n", 4) == 0)
	    break;
    }

    if (i == size) {
	debug_printf("http header:\n%s\n", buffer);
	return SR_ERROR_NO_HTTP_HEADER;
    }

    buffer[i] = '\0';

#ifdef WIN32
    timeout = rmi->prefs->timeout * 1000;  /* Convert sec to msec */
    if (setsockopt (socket_handle->s, SOL_SOCKET,  SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) == SOCKET_ERROR)
	return SR_ERROR_CANT_SET_SOCKET_OPTIONS;
#endif

    return SR_SUCCESS;
}

static error_code
socklib_recv_raw (RIP_MANAGER_INFO* rmi, HSOCKET *socket_handle,
		 char* buffer, int size, int timeout)
{
    int ret = 0, read = 0;
    SOCKET sock;
    fd_set fds;
    struct timeval tv;
    
    sock = socket_handle->s;
    FD_ZERO(&fds);
    while(size) {
	if (socket_handle->closed)
	    return SR_ERROR_SOCKET_CLOSED;
	
	if (timeout > 0
#ifdef HAVE_OPENSSL
            && (!socket_handle->tls || SSL_pending((SSL *)socket_handle->tls) == 0)
            && (!socket_handle->proxy_tls || SSL_pending((SSL *)socket_handle->proxy_tls) == 0)
#endif
        ) {
	    /* Wait up to 'timeout' seconds for data on socket to be 
	       ready for read */
#if __UNIX__
	    FD_SET(rmi->abort_pipe[0], &fds);
#endif
	    FD_SET(sock, &fds);
	    tv.tv_sec = timeout;
	    tv.tv_usec = 0;
	    ret = select (sock + 1, &fds, NULL, NULL, &tv);
	    if (ret == SOCKET_ERROR) {
		/* This happens when I kill winamp while ripping */
		return SR_ERROR_SELECT_FAILED;
	    }
	    if (ret == 0) {
		return SR_ERROR_TIMEOUT;
	    }
	}
#if __UNIX__
	if (FD_ISSET(rmi->abort_pipe[0], &fds)) {
	    debug_printf ("socklib_recvall detected write to abort pipe.\n");
	    return SR_ERROR_ABORT_PIPE_SIGNALLED;
	}
#endif
#ifdef HAVE_OPENSSL
        if (socket_handle->tls) {
            ret = SSL_read((SSL *)socket_handle->tls, &buffer[read], size);
            if (ret <= 0) {
                int error = SSL_get_error((SSL *)socket_handle->tls, ret);
                if (error == SSL_ERROR_ZERO_RETURN) break;
                return SR_ERROR_RECV_FAILED;
            }
        } else
#endif
        ret = recv(socket_handle->s, &buffer[read], size, 0);
	debug_printf ("RECV req %5d bytes, got %5d bytes\n", size, ret);

        if (ret == SOCKET_ERROR) {
	    debug_printf ("RECV failed, errno = %d\n", errno);
	    debug_printf ("Err = %s\n",strerror(errno));
	    return SR_ERROR_RECV_FAILED;
	}

	/* Got zero bytes on blocking read.  For unix this is an 
	   orderly shutdown. */
	if (ret == 0) {
	    debug_printf ("recv received zero bytes!\n");
	    break;
	}

	read += ret;
	size -= ret;
    }

    return read;
}

/* Decode HTTP transfer framing before ICY's byte counters see the body. */
static error_code
socklib_chunk_line (RIP_MANAGER_INFO *rmi, HSOCKET *sock, char *line, int capacity, int timeout)
{
    int i;
    for (i = 0; i < capacity - 1; i++) {
        int rc = socklib_recv_raw(rmi, sock, line + i, 1, timeout);
        if (rc < 0) return rc;
        if (rc != 1) return SR_ERROR_RECV_FAILED;
        if (line[i] == '\n') {
            if (i == 0 || line[i - 1] != '\r') return SR_ERROR_RECV_FAILED;
            line[i - 1] = 0;
            return SR_SUCCESS;
        }
    }
    return SR_ERROR_RECV_FAILED;
}

error_code
socklib_recvall (RIP_MANAGER_INFO *rmi, HSOCKET *sock, char *buffer, int size, int timeout)
{
    int total = 0;
    if (!sock->chunked) return socklib_recv_raw(rmi, sock, buffer, size, timeout);
    while (size > 0 && !sock->chunk_done) {
        int rc, count;
        if (!sock->chunk_remaining) {
            char line[1024], *p;
            guint64 length = 0;
            if (sock->chunk_crlf) {
                char crlf[2];
                rc = socklib_recv_raw(rmi, sock, crlf, 2, timeout);
                if (rc < 0) return rc;
                if (rc != 2 || memcmp(crlf, "\r\n", 2)) return SR_ERROR_RECV_FAILED;
                sock->chunk_crlf = FALSE;
            }
            rc = socklib_chunk_line(rmi, sock, line, sizeof(line), timeout);
            if (rc < 0) return rc;
            p = line;
            if (!g_ascii_isxdigit(*p)) return SR_ERROR_RECV_FAILED;
            while (g_ascii_isxdigit(*p)) {
                int digit = g_ascii_xdigit_value(*p++);
                if (length > (G_MAXUINT64 - digit) / 16) return SR_ERROR_RECV_FAILED;
                length = length * 16 + digit;
            }
            while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != ';') return SR_ERROR_RECV_FAILED;
            sock->chunk_remaining = length;
            if (!length) {
                int trailer_bytes = 0;
                do {
                    rc = socklib_chunk_line(rmi, sock, line, sizeof(line), timeout);
                    if (rc < 0) return rc;
                    trailer_bytes += strlen(line) + 2;
                    if (trailer_bytes > 65536) return SR_ERROR_RECV_FAILED;
                } while (*line);
                sock->chunk_done = TRUE;
                break;
            }
        }
        count = sock->chunk_remaining < (guint64)size ? (int)sock->chunk_remaining : size;
        rc = socklib_recv_raw(rmi, sock, buffer + total, count, timeout);
        if (rc < 0) return rc;
        if (rc != count) return SR_ERROR_RECV_FAILED;
        total += rc;
        size -= rc;
        sock->chunk_remaining -= rc;
        if (!sock->chunk_remaining) sock->chunk_crlf = TRUE;
    }
    return total;
}

int
socklib_sendall (HSOCKET *socket_handle, char* buffer, int size)
{
    int ret = 0, sent = 0;

    while(size) {
	if (socket_handle->closed)
	    return SR_ERROR_SOCKET_CLOSED;

#ifdef HAVE_OPENSSL
        if (socket_handle->tls)
            ret = SSL_write((SSL *)socket_handle->tls, &buffer[sent], size);
        else
#endif
        ret = send(socket_handle->s, &buffer[sent], size, 0);
        if (ret <= 0)
	    return SR_ERROR_SEND_FAILED;

	if (ret == 0)
	    break;

	sent += ret;
	size -= ret;
    }

    return sent;
}
