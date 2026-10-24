#ifndef __socklib_h__
#define __socklib_h__

#include "srtypes.h"
#include "rip_manager.h"

#ifndef INADDR_NONE
#define INADDR_NONE (-1)
#endif

error_code socklib_init ();
error_code socklib_open (HSOCKET *socket_handle, char *host, int port, char *if_name, int timeout, int ip_version);
void socklib_close (HSOCKET *socket_handle);
#ifdef HAVE_OPENSSL
error_code socklib_start_tls (HSOCKET *socket_handle, const char *host, const STREAM_PREFS *prefs, BOOL proxy);
void socklib_free_tls (HSOCKET *socket_handle);
#endif
void socklib_cleanup ();
error_code
socklib_read_header(RIP_MANAGER_INFO* rmi, HSOCKET *socket_handle, 
		    char *buffer, int size);
error_code
socklib_recvall (RIP_MANAGER_INFO* rmi, HSOCKET *socket_handle, 
		 char* buffer, int size, int timeout);
int socklib_sendall (HSOCKET *socket_handle, char* buffer, int size);
error_code read_interface (char *if_name, uint32_t *addr);
error_code socklib_bind_interface(SOCKET sock, int family, const char *if_name);

#endif	//__socklib_h__
