#ifndef NET_H
#define NET_H

// Standard headers
#include <stdatomic.h>
#include <stdint.h>

#ifdef __APPLE__
#define PLATFORM_MAC
#endif

#if defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) ||               \
    defined(__MINGW32__) || defined(__BORLANDC__)
#define PLATFORM_WIN
#endif

#ifdef __linux__
#define PLATFORM_NIX
#endif

// Platform dependant headers.
#ifdef PLATFORM_NIX
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define NetSocket int
#endif

#ifdef PLATFORM_WIN
#define NetSocket Socket
#endif

#ifdef PLATFORM_MAC
#define NetSocket int
#endif

// If we still don't have INVALID_SOCKET after including headers, set it.
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

//! An ssl handle and context container.
typedef struct _net_ssl {
    SSL_CTX *context;
    SSL *handle;
} net_ssl;

void ssl_init(void);
void ssl_cleanup(void);

//! A network connection representation.
typedef struct _net_connection {
    NetSocket sock;
    net_ssl ssl;
    char *ip;
    unsigned short port;
    atomic_char ready;
    char *certificate;
    char *private_key;
} net_connection;

void net_connection_init(net_connection *conn);
void net_connection_free(net_connection *conn);

//! A network message.
typedef struct _net_message {
    char *data;    // Source data.
    uint32_t size; // Source data size.
} net_message;

//! Initialize net_message members.
void net_message_init(net_message *msg);

//! Free internal net_message data.
void net_message_free(net_message *msg);

//! Return a socket bound to host:port.
net_connection *net_bind_ptr(const char *host, const char *port,
                             int (*socket_f)(int, int, int),
                             int (*bind_f)(int, const struct sockaddr *,
                                           socklen_t),
                             int (*listen_f)(int, int));
net_connection *net_bind(const char *host, const char *port);

//! Accept a new socket.
net_connection *
net_accept_ptr(net_connection *server, struct sockaddr_in *sa, socklen_t *len,
               int (*accept_f)(int, struct sockaddr *, socklen_t *),
               SSL_CTX *(*ssl_ctx_new_f)(const SSL_METHOD *),
               int (*ssl_ctx_use_cert_f)(SSL_CTX *, const char *, int),
               int (*ssl_ctx_use_key_f)(SSL_CTX *, const char *, int),
               SSL *(*ssl_new_f)(SSL_CTX *), int (*ssl_set_f)(SSL *, int),
               int (*ssl_accept_f)(SSL *));
net_connection *net_accept(net_connection *server, struct sockaddr_in *sa,
                           socklen_t *len);

//! Return a socket connected to host:port.
net_connection *
net_connect_ptr(const char *host, const char *port,
                int (*socket_f)(int, int, int),
                int (*connect_f)(int, const struct sockaddr *, socklen_t len),
                SSL_CTX *(*ssl_ctx_new_f)(const SSL_METHOD *),
                SSL *(*ssl_new_f)(SSL_CTX *), int (*ssl_set_f)(SSL *, int),
                int (*ssl_connect_f)(SSL *));
net_connection *net_connect(const char *host, const char *port);

//! Send a net_message.
NetSocket net_send_ptr(net_connection *conn, net_message *msg,
                       int (*ssl_send_f)(SSL *, const void *, int));
NetSocket net_send(net_connection *conn, net_message *msg);

//! Receive a net_message.
NetSocket net_recv_ptr(net_connection *conn, net_message *msg,
                       int (*ssl_recv_f)(SSL *, void *, int));
NetSocket net_recv(net_connection *conn, net_message *msg);

//! Close a socket.
NetSocket net_close(NetSocket sock);

//! Get address information via this simple wrapper.
#ifdef PLATFORM_NIX
struct addrinfo *getaddrinfo_easy(const char *host, const char *port);
#endif

#endif /* NET_H */
