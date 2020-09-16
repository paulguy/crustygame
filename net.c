#include "net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//! Globally initialize SSL.
void ssl_init(void)
{
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

//! Globally cleanup SSL.
void ssl_cleanup(void)
{
    ERR_free_strings();
    EVP_cleanup();
}

//! Initialize a single net_connection.
void net_connection_init(net_connection *conn)
{
    memset(conn, 0, sizeof(net_connection));
}

//! Free a single net_connection.
void net_connection_free(net_connection *conn)
{
    if (conn->ssl.handle) {
        SSL_shutdown(conn->ssl.handle);
        SSL_free(conn->ssl.handle);
        conn->ssl.handle = NULL;
    }
    if (conn->ssl.context) {
        SSL_CTX_free(conn->ssl.context);
        conn->ssl.context = NULL;
    }
}

void net_message_init(net_message *msg)
{
    memset(msg, 0, sizeof(net_message));
}

void net_message_free(net_message *msg)
{
    if (msg->data) {
        free(msg->data);
        msg->data = NULL;
        msg->size = 0;
    }
}

#ifdef PLATFORM_NIX
// Linux-specific definitions to platform independent functions.
net_connection *net_bind_ptr(const char *host, const char *port,
                             int (*socket_f)(int, int, int),
                             int (*bind_f)(int, const struct sockaddr *,
                                           socklen_t),
                             int (*listen_f)(int, int))
{
    // Convert the port before initializing anything, in case the input
    // given was invalid.
    int n_port = strtol(port, NULL, 10);
    if (n_port < 1 || n_port > 65535)
        return NULL;

    struct addrinfo *ptr = getaddrinfo_easy(host, port);
    if (!ptr) {
        fprintf(stderr, "Unable to resolve %s:%s; errno: '%s'\n", host, port,
                strerror(errno));
        return NULL;
    }

    net_connection *server = (net_connection *)malloc(sizeof(net_connection));
    net_connection_init(server);

    // Loop variables.
    struct addrinfo *current;
    int error = 0;

    // Loop through each address returned until we bind without error.
    for (current = ptr; current != NULL; current = current->ai_next) {
        server->sock = socket_f(current->ai_family, current->ai_socktype,
                                current->ai_protocol);
        if (server->sock == INVALID_SOCKET)
            break;

        int val = 1;
        setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));

        error = bind_f(server->sock, current->ai_addr, current->ai_addrlen);
        if (error != -1) {
            // Store server ip and port.
            server->ip =
                inet_ntoa(((struct sockaddr_in *)current->ai_addr)->sin_addr);
            server->port =
                htons(((struct sockaddr_in *)current->ai_addr)->sin_port);
            break;
        }

        // Close the socket since we were unable to bind.
        close(server->sock);
        server->sock = INVALID_SOCKET;
    }

    // Free the address information we collected.
    freeaddrinfo(ptr);

    if (error == -1 || listen(server->sock, 10) == INVALID_SOCKET) {
        fprintf(stderr, "Listen(10) failed; errno: '%s'\n", strerror(errno));
        close(server->sock);
        server->sock = INVALID_SOCKET;
        free(server);
        server = NULL;
    }

    return server;
}

net_connection *net_bind(const char *host, const char *port)
{
    return net_bind_ptr(host, port, &socket, &bind, &listen);
}

net_connection *
net_accept_ptr(net_connection *server, struct sockaddr_in *sa, socklen_t *len,
               int (*accept_f)(int, struct sockaddr *, socklen_t *),
               SSL_CTX *(*ssl_ctx_new_f)(const SSL_METHOD *),
               int (*ssl_ctx_use_cert_f)(SSL_CTX *, const char *, int),
               int (*ssl_ctx_use_key_f)(SSL_CTX *, const char *, int),
               SSL *(*ssl_new_f)(SSL_CTX *), int (*ssl_set_f)(SSL *, int),
               int (*ssl_accept_f)(SSL *))
{
    *len = sizeof(struct sockaddr_in);

    net_connection *conn = (net_connection *)malloc(sizeof(net_connection));
    net_connection_init(conn);

    conn->sock = accept_f(server->sock, (struct sockaddr *)sa, len);
    if (conn->sock <= 0) {
        fprintf(stderr, "Failed to accept connection; errno: '%s'\n",
                strerror(errno));
        free(conn);
        return NULL;
    }

    conn->ip = inet_ntoa(sa->sin_addr);
    conn->port = htons(sa->sin_port);

    conn->ssl.context = ssl_ctx_new_f(SSLv23_server_method());
    SSL_CTX_set_ecdh_auto(conn->ssl.context, 1);

    if (ssl_ctx_use_cert_f(conn->ssl.context, server->certificate,
                           SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    if (ssl_ctx_use_key_f(conn->ssl.context, server->private_key,
                          SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    conn->ssl.handle = ssl_new_f(conn->ssl.context);
    ssl_set_f(conn->ssl.handle, conn->sock);

    int error = ssl_accept_f(conn->ssl.handle);
    if (error <= 0) {
        fprintf(stderr, "SSL_accept returned %d; errno: '%s'\n", error,
                strerror(errno));
        net_connection_free(conn);
        free(conn);
        conn = NULL;
    } else {
        conn->ready = 1;
    }
    return conn;
}

net_connection *net_accept(net_connection *server, struct sockaddr_in *sa,
                           socklen_t *len)
{
    return net_accept_ptr(
        server, sa, len, &accept, &SSL_CTX_new, &SSL_CTX_use_certificate_file,
        &SSL_CTX_use_PrivateKey_file, &SSL_new, &SSL_set_fd, &SSL_accept);
}

net_connection *
net_connect_ptr(const char *host, const char *port,
                int (*socket_f)(int, int, int),
                int (*connect_f)(int, const struct sockaddr *, socklen_t len),
                SSL_CTX *(*ssl_ctx_new_f)(const SSL_METHOD *),
                SSL *(*ssl_new_f)(SSL_CTX *), int (*ssl_set_f)(SSL *, int),
                int (*ssl_connect_f)(SSL *))
{
    struct addrinfo *ptr = getaddrinfo_easy(host, port);

    // Search for host:port.
    if (!ptr) {
        fprintf(stderr, "unable to resolve %s:%s...\n", host, port);
        return NULL;
    }

    net_connection *client = (net_connection *)malloc(sizeof(net_connection));
    net_connection_init(client);

    struct addrinfo *current;
    int error = 0;

    for (current = ptr; current != NULL; current = current->ai_next) {
        client->sock = socket_f(current->ai_family, current->ai_socktype,
                                current->ai_protocol);
        if (client->sock == INVALID_SOCKET)
            break;

        error = connect_f(client->sock, current->ai_addr, current->ai_addrlen);
        if (error != -1) {
            // Store client ip and port.
            struct sockaddr_in local;
            memset(&local, 0, sizeof(struct sockaddr_in));
            socklen_t len = sizeof(struct sockaddr_in);
            getsockname(client->sock, (struct sockaddr *)&local, &len);
            client->ip = inet_ntoa(local.sin_addr);
            client->port = htons(local.sin_port);
            break;
        }

        close(client->sock);
        client->sock = INVALID_SOCKET;
    }

    freeaddrinfo(ptr);

    if (client->sock == -1 || error == -1) {
        free(client);
        client = NULL;
    } else {
        client->ssl.context = ssl_ctx_new_f(SSLv23_client_method());
        if (!client->ssl.context)
            return NULL;
        SSL_CTX_set_ecdh_auto(client->ssl.context, 1);
        SSL_CTX_set_verify(client->ssl.context, SSL_VERIFY_NONE, NULL);
        client->ssl.handle = SSL_new(client->ssl.context);
        ssl_set_f(client->ssl.handle, client->sock);
        if (ssl_connect_f(client->ssl.handle) <= 0) {
            fprintf(stderr, "SSL_connect() failed: '%s'\n", strerror(errno));
            return NULL;
        }

        client->ready = 1;
    }

    return client;
}

net_connection *net_connect(const char *host, const char *port)
{
    return net_connect_ptr(host, port, &socket, &connect, &SSL_CTX_new,
                           &SSL_new, &SSL_set_fd, &SSL_connect);
}
#endif /* PLATFORM_NIX */

int net_send_ptr(net_connection *conn, net_message *msg,
                 int (*ssl_send_f)(SSL *, const void *, int))
{
    // First, send the size of bytes we're sending. If that fails,
    // return the bytes we actually sent.
    int bytes = ssl_send_f(conn->ssl.handle, &msg->size, sizeof(uint32_t));
    if (bytes != sizeof(uint32_t))
        return bytes;

    // If that succeeds, continue to send the message's data.
    bytes = ssl_send_f(conn->ssl.handle, msg->data, msg->size);
    return bytes;
}

int net_send(net_connection *conn, net_message *msg)
{
    return net_send_ptr(conn, msg, &SSL_write);
}

int net_recv_ptr(net_connection *conn, net_message *msg,
                 int (*ssl_read_f)(SSL *, void *, int))
{
    uint32_t size = 0;

    int bytes = ssl_read_f(conn->ssl.handle, (char *)&size, sizeof(uint32_t));
    if (bytes != sizeof(uint32_t))
        return bytes;

    if (msg->data) {
        if (size != msg->size) {
            msg->size = size;
            msg->data = (char *)realloc(msg->data, sizeof(char) * msg->size);
        }
    } else {
        msg->size = size;
        msg->data = (char *)malloc(sizeof(char) * msg->size);
    }

    bytes = ssl_read_f(conn->ssl.handle, msg->data, msg->size);
    return bytes;
}

int net_recv(net_connection *conn, net_message *msg)
{
    return net_recv_ptr(conn, msg, &SSL_read);
}

#ifdef PLATFORM_NIX
NetSocket net_close(NetSocket sock)
{
    close(sock);
    sock = INVALID_SOCKET;
    return sock;
}
#endif /* PLATFORM_NIX */

#ifdef PLATFORM_WIN
NetSocket net_close(NetSocket sock)
{
    // TODO: Implement Windows socket close.
    return -1;
}
#endif /* PLATFORM_WIN */

#ifdef PLATFORM_MAC
NetSocket net_close(NetSocket sock)
{
    close(sock);
    sock = INVALID_SOCKET;
    return sock;
}
#endif /* PLATFORM_MAC */

#ifdef PLATFORM_NIX
struct addrinfo *getaddrinfo_easy(const char *host, const char *port)
{
    struct addrinfo hints, *results = NULL;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(host, port, &hints, &results) != 0)
        return NULL;
    return results;
}
#endif /* PLATFORM_NIX */
