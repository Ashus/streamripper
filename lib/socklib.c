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
socklib_open (HSOCKET *socket_handle, char *host, int port, 
	      char *if_name, int timeout)
{
    int rc;
    struct sockaddr_in address, local;
    struct hostent *hp;
    int len;

    if (!socket_handle || !host)
	return SR_ERROR_INVALID_PARAM;

    socket_handle->closed = TRUE;
    socket_handle->chunked = FALSE;
    socket_handle->chunk_done = FALSE;
    socket_handle->chunk_crlf = FALSE;
    socket_handle->chunk_remaining = 0;

    /* On error:
       Unix returns -1 and sets errno.
       Windows??? */
#ifdef HAVE_OPENSSL
    socket_handle->tls = NULL;
#endif
    socket_handle->s = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle->s == SOCKET_ERROR) {
	debug_printf ("socket() failed\n");
	return SR_ERROR_CANT_CREATE_SOCKET;
    }

    if (if_name) {
	if (read_interface (if_name, (uint32_t *)&local.sin_addr.s_addr) != 0)
	    local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_family = AF_INET;
	local.sin_port = htons(0);
	/* On error:
	   Unix returns -1 and sets errno.
	   Windows??? */
	debug_printf ("Calling bind\n");
	if (bind(socket_handle->s, (struct sockaddr *)&local, 
		 sizeof(local)) == SOCKET_ERROR) {
	    debug_printf ("Bind failed\n");
	    closesocket (socket_handle->s);
	    return SR_ERROR_CANT_BIND_ON_INTERFACE;
	}
    }

    if ((address.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE) {
	debug_printf ("Calling gethostbyname\n");
	hp = gethostbyname (host);
	if (hp) {
	    memcpy (&address.sin_addr, hp->h_addr_list[0], hp->h_length);
	} else {
	    debug_printf ("resolving hostname: %s failed\n", host);
	    /* GCS Added... */
	    closesocket (socket_handle->s);
	    return SR_ERROR_CANT_RESOLVE_HOSTNAME;
	}
    }
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    len = sizeof(address);

    debug_printf ("Calling connect\n");
    rc = connect (socket_handle->s, (struct sockaddr *)&address, len);
    debug_printf ("Connect complete\n");
    if (rc == SOCKET_ERROR) {
	debug_printf ("connect failed\n");
	/* GCS Added... */
	closesocket (socket_handle->s);
	return SR_ERROR_CONNECT_FAILED;
    }

#ifdef WIN32
    {
	DWORD tv = (timeout > 0 ? timeout : 15) * 1000;
        setsockopt(socket_handle->s, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
	rc = setsockopt (socket_handle->s, SOL_SOCKET,  SO_RCVTIMEO, 
			 (char *)&tv, sizeof(tv));
	if (rc == SOCKET_ERROR) {
	    debug_printf ("setsockopt failed\n");
	    /* GCS Added... */
	    closesocket (socket_handle->s);
	    return SR_ERROR_CANT_SET_SOCKET_OPTIONS;
	}
    }
#endif

    socket_handle->closed = FALSE;
    return SR_SUCCESS;
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
}

error_code
socklib_start_tls (HSOCKET *socket_handle, const char *host, const STREAM_PREFS *prefs)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL *ssl;
    unsigned char ip[16];
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
    socket_handle->tls = ssl;
    ERR_clear_error();
    if (inet_pton(AF_INET, host, ip) == 1 || inet_pton(AF_INET6, host, ip) == 1) {
        if (!X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), host)) goto failed;
    } else {
        if (!SSL_set_tlsext_host_name(ssl, host) || !SSL_set1_host(ssl, host)) goto failed;
    }
    if (!SSL_set_fd(ssl, (int)socket_handle->s) || SSL_connect(ssl) != 1) goto failed;
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
        fprintf(stdout, "TLS: %s; cipher: %s; group: %s; signature: %s; verification: %s\n",
                SSL_get_version(ssl), SSL_get_cipher(ssl), group ? group : "unknown",
                signature ? signature : "unknown", prefs->tls_insecure ? "disabled" : "enabled");
        fflush(stdout);
    }
    return SR_SUCCESS;
failed:
    fprintf(stderr, "TLS error for %s: %s (%s)\n", host,
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
