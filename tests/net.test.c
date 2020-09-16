// Test headers.
#include "unity/unity.h"

// Local headers.
#include "../net.h"
#include "../x509.h"

#include <pthread.h>
#include <string.h>

// Test-wise macros.
#define CERT_PATH "/tmp/net.test.cert.pem";
#define KEY_PATH "/tmp/net.test.key.pem";

// Some globals.
int mock_bind_count = 0;

static int mock_bind(int sock, const struct sockaddr *sa, socklen_t len)
{
    if (mock_bind_count++ > 0)
        return sock;
    return -1;
}

static int mock_bind_bad(int sock, const struct sockaddr *sa, socklen_t len)
{
    return -1;
}

static int mock_socket(int family, int socktype, int protocol)
{
    return -1;
}

static int mock_accept(int fd, struct sockaddr *sa, socklen_t *len)
{
    return -1;
}

static SSL_CTX *mock_ssl_ctx_new(const SSL_METHOD *method)
{
    return NULL;
}

static SSL *mock_ssl_new(SSL_CTX *ctx)
{
    return NULL;
}

static int mock_ssl_use_cert(SSL_CTX *ctx, const char *path, int param)
{
    return -1;
}

static int mock_ssl_use_key(SSL_CTX *ctx, const char *path, int param)
{
    return -1;
}

static int mock_ssl_set(SSL *handle, int fd)
{
    return 0;
}

static int mock_ssl_accept(SSL *handle)
{
    return -1;
}

static int mock_ssl_connect(SSL *handle)
{
    return -1;
}

static int mock_connect_good(int sock, const struct sockaddr *sa,
                             socklen_t len)
{
    return sock;
}

static int mock_net_send(SSL *handle, const void *buf, int bytes)
{
    return -1;
}

static int mock_net_recv(SSL *handle, void *buf, int bytes)
{
    return -1;
}

typedef struct _client_mock_functions {
    int (*socket_f)(int, int, int);
    int (*connect_f)(int, const struct sockaddr *, socklen_t);
    SSL_CTX *(*ssl_ctx_new_f)(const SSL_METHOD *);
    SSL *(*ssl_new_f)(SSL_CTX *);
    int (*ssl_set_f)(SSL *, int);
    int (*ssl_connect_f)(SSL *);
} client_mock_functions;

// Test functions.
void setUp(void)
{
    ssl_init();
}

void tearDown(void)
{
    ssl_cleanup();
}

void test_net_ssl_initializes(void)
{
    // Do nothing, setUp/tearDown exercises SSL.
}

void test_getaddrinfo_failures(void)
{
    TEST_ASSERT_TRUE(getaddrinfo_easy("BLAHBLAH!", "31337") == NULL);
}

static void *server_thread(void *data)
{
    net_connection *server = (net_connection *)data;

    struct sockaddr_in sa;
    socklen_t len = sizeof(struct sockaddr_in);
    memset(&sa, 0, sizeof(struct sockaddr_in));

    net_connection *conn = net_accept(server, &sa, &len);

    net_message msg;
    net_message_init(&msg);
    TEST_ASSERT_FALSE(net_recv(conn, &msg) <= 0);
    printf("received: %s\n", msg.data);

    msg.size = 0;
    TEST_ASSERT_FALSE(net_recv(conn, &msg) <= 0);
    printf("received: %s\n", msg.data);

    net_message_free(&msg);

    conn->sock = net_close(conn->sock);
    net_connection_free(conn);
    free(conn);
    return NULL;
}

void test_net_bind_localhost(void)
{
    const char *const host = "localhost";
    const char *const port = "31337";

    net_connection *server = net_bind(host, port);
    TEST_ASSERT_TRUE(server != NULL);
    printf("Server listening on %s:%u...\n", server->ip, server->port);

    server->certificate = (char *)CERT_PATH;
    server->private_key = (char *)KEY_PATH;

    // Start the server thread.
    pthread_t tid = 0;
    pthread_create(&tid, NULL, &server_thread, (void *)server);

    // Connect to the server.
    printf("Connecting to %s:%s...\n", host, port);
    net_connection *client = net_connect(host, port);
    TEST_ASSERT_TRUE(client != NULL);

    printf("Connected from %s:%u!\n", client->ip, client->port);

    net_message msg;
    msg.data = (char *)"test";
    msg.size = 5;
    TEST_ASSERT_FALSE(net_send(client, &msg) <= 0);
    printf("sent: %s\n", msg.data);

    TEST_ASSERT_FALSE(net_send(client, &msg) <= 0);
    printf("sent: %s\n", msg.data);

    TEST_ASSERT_TRUE(net_send_ptr(client, &msg, &mock_net_send) <= 0);
    TEST_ASSERT_TRUE(net_recv_ptr(client, &msg, &mock_net_recv) <= 0);

    net_connection_free(client);
    free(client);

    pthread_join(tid, NULL); // Wait for the server thread to exit.

    server->sock = net_close(server->sock);
    net_connection_free(server);
    free(server);
}

void test_net_bind_failures(void)
{
    TEST_ASSERT_FALSE(net_bind("localhost", "0"));
    TEST_ASSERT_FALSE(net_bind("BLAHBLAH!", "1"));

    net_connection *server =
        net_bind_ptr("localhost", "31337", &socket, &mock_bind, &listen);
    TEST_ASSERT_TRUE(server != NULL);

    server->sock = net_close(server->sock);
    net_connection_free(server);
    free(server);

    server =
        net_bind_ptr("localhost", "31337", &socket, &mock_bind_bad, &listen);
    TEST_ASSERT_TRUE(server == NULL);
}

//! @param data Array of 6 void * function pointers.
static void *mock_client_thread(void *data)
{
    client_mock_functions *func = (client_mock_functions *)data;

    net_connection *client =
        net_connect_ptr("localhost", "31337", func->socket_f, func->connect_f,
                        func->ssl_ctx_new_f, func->ssl_new_f, func->ssl_set_f,
                        func->ssl_connect_f);
    if (client) {
        net_connection_free(client);
        free(client);
    }

    return NULL;
}

void test_net_accept_failures(void)
{
    const char *const host = "localhost";
    const char *const port = "31337";

    net_connection *server = net_bind(host, port);
    TEST_ASSERT_TRUE(server != NULL);
    printf("Server listening on %s:%u...\n", server->ip, server->port);

    server->certificate = (char *)CERT_PATH;
    server->private_key = (char *)KEY_PATH;

    struct sockaddr_in sa;
    socklen_t len = sizeof(struct sockaddr_in);

    TEST_ASSERT_FALSE(net_accept_ptr(
        server, &sa, &len, &mock_accept, &mock_ssl_ctx_new, &mock_ssl_use_cert,
        &mock_ssl_use_key, &mock_ssl_new, &mock_ssl_set, &mock_ssl_accept));

    // We need to accept a connection from another thread here; since
    // we are now using valid accept.

    client_mock_functions client_mock = {.socket_f = &socket,
                                         .connect_f = &connect,
                                         .ssl_ctx_new_f = &mock_ssl_ctx_new,
                                         .ssl_new_f = &mock_ssl_new,
                                         .ssl_set_f = &mock_ssl_set,
                                         .ssl_connect_f = &mock_ssl_connect};

    pthread_t tid = 0;
    TEST_ASSERT_TRUE(
        pthread_create(&tid, NULL, &mock_client_thread, &client_mock) == 0);
    TEST_ASSERT_TRUE(net_accept_ptr(server, &sa, &len, &accept,
                                    &mock_ssl_ctx_new, &mock_ssl_use_cert,
                                    &mock_ssl_use_key, &mock_ssl_new,
                                    &mock_ssl_set, &mock_ssl_accept) == NULL);
    pthread_join(tid, NULL);

    pthread_create(&tid, NULL, &mock_client_thread, &client_mock);
    TEST_ASSERT_FALSE(net_accept_ptr(
        server, &sa, &len, &accept, &SSL_CTX_new, &mock_ssl_use_cert,
        &mock_ssl_use_key, &mock_ssl_new, &mock_ssl_set, &mock_ssl_accept));
    pthread_join(tid, NULL);

    pthread_create(&tid, NULL, &mock_client_thread, &client_mock);
    TEST_ASSERT_FALSE(net_accept_ptr(server, &sa, &len, &accept, &SSL_CTX_new,
                                     &SSL_CTX_use_certificate_file,
                                     &mock_ssl_use_key, &mock_ssl_new,
                                     &mock_ssl_set, &mock_ssl_accept));
    pthread_join(tid, NULL);

    pthread_create(&tid, NULL, &mock_client_thread, &client_mock);
    TEST_ASSERT_FALSE(net_accept_ptr(server, &sa, &len, &accept, &SSL_CTX_new,
                                     &SSL_CTX_use_certificate_file,
                                     &SSL_CTX_use_PrivateKey_file, &SSL_new,
                                     &mock_ssl_set, &mock_ssl_accept));
    pthread_join(tid, NULL);

    pthread_create(&tid, NULL, &mock_client_thread, &client_mock);
    TEST_ASSERT_FALSE(net_accept_ptr(server, &sa, &len, &accept, &SSL_CTX_new,
                                     &SSL_CTX_use_certificate_file,
                                     &SSL_CTX_use_PrivateKey_file, &SSL_new,
                                     &SSL_set_fd, &mock_ssl_accept));
    pthread_join(tid, NULL);

    // Close server->sock.
    server->sock = net_close(server->sock);
}

void test_net_connect_failures(void)
{
    const char *const host = "localhost";
    const char *const port = "31337";

    net_connection *client = net_connect(host, port);
    TEST_ASSERT_TRUE(client == NULL);

    client = net_connect("BLAHBLAH!", "1");
    TEST_ASSERT_TRUE(client == NULL);

    client = net_connect_ptr(host, port, &mock_socket, NULL, NULL, NULL, NULL,
                             NULL);
    TEST_ASSERT_TRUE(client == NULL);

    client =
        net_connect_ptr(host, port, &socket, &mock_connect_good, &SSL_CTX_new,
                        &SSL_new, &SSL_set_fd, &mock_ssl_connect);
    TEST_ASSERT_TRUE(client == NULL);
}

int main(int argc, char *argv[])
{
    printf("Generating PEM certificate...\n");
    X509 *x509 = NULL;
    EVP_PKEY *pkey = NULL;
    make_certificate_easy(&x509, &pkey, "localhost");

    write_certificate(x509, "/tmp/net.test.cert.pem");
    write_certificate_key(pkey, "/tmp/net.test.key.pem");

    EVP_PKEY_free(pkey);
    X509_free(x509);

    UNITY_BEGIN();
    RUN_TEST(test_net_ssl_initializes);
    RUN_TEST(test_getaddrinfo_failures);
    RUN_TEST(test_net_bind_localhost);
    RUN_TEST(test_net_bind_failures);
    RUN_TEST(test_net_accept_failures);
    RUN_TEST(test_net_connect_failures);
    return 0;
}
