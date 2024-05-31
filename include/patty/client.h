#ifndef _PATTY_CLIENT_H
#define _PATTY_CLIENT_H

#define PATTY_CLIENT_DEFAULT_SOCK_NAME "patty.sock"

enum patty_client_call {
    PATTY_CLIENT_NONE,
    PATTY_CLIENT_PING,
    PATTY_CLIENT_SOCKET,
    PATTY_CLIENT_SETSOCKOPT,
    PATTY_CLIENT_BIND,
    PATTY_CLIENT_LISTEN,
    PATTY_CLIENT_ACCEPT,
    PATTY_CLIENT_CONNECT,
    PATTY_CLIENT_CLOSE,
    PATTY_CLIENT_SENDTO,
    PATTY_CLIENT_RECVFROM,
    PATTY_CLIENT_CALL_COUNT
};

typedef struct _patty_client_sock patty_client_sock;
typedef struct _patty_client patty_client;

patty_client *patty_client_new(const char *path);

void patty_client_destroy(patty_client *client);

ssize_t patty_client_read(patty_client *client, void *buf, size_t len);

ssize_t patty_client_write(patty_client *client, const void *buf, size_t len);

/*
 * ping()
 */
int patty_client_ping(patty_client *client);

/*
 * socket()
 */
typedef struct _patty_client_socket_request {
    int proto;
    int type;
} patty_client_socket_request;

typedef struct _patty_client_socket_response {
    int fd;
    int eno;
    char path[PATTY_AX25_SOCK_PATH_SIZE];
} patty_client_socket_response;

int patty_client_socket(patty_client *client,
                        int proto,
                        int type);

/*
 * setsockopt()
 */
typedef struct _patty_client_setsockopt_request {
    int fd;
    int opt;
    size_t len;
} patty_client_setsockopt_request;

typedef struct _patty_client_setsockopt_params {
    uint32_t flags;

    size_t mtu,
           window,
           retry;

    time_t ack;
} patty_client_setsockopt_params;

typedef struct _patty_client_setsockopt_if {
    char name[8];
    int state;
} patty_client_setsockopt_if;

typedef struct _patty_client_setsockopt_response {
    int ret;
    int eno;
} patty_client_setsockopt_response;

int patty_client_setsockopt(patty_client *client,
                            int fd,
                            int opt,
                            void *data,
                            size_t len);

/*
 * bind()
 */
typedef struct _patty_client_bind_request {
    int fd;
    patty_ax25_addr addr;
} patty_client_bind_request;

typedef struct _patty_client_bind_response {
    int ret;
    int eno;
} patty_client_bind_response;

int patty_client_bind(patty_client *client,
                      int fd,
                      patty_ax25_addr *addr);

/*
 * listen()
 */
typedef struct _patty_client_listen_request {
    int fd;
} patty_client_listen_request;

typedef struct _patty_client_listen_response {
    int ret;
    int eno;
} patty_client_listen_response;

int patty_client_listen(patty_client *client,
                        int fd);

/*
 * accept()
 */
typedef struct _patty_client_accept_request {
    int fd;
} patty_client_accept_request;

typedef struct _patty_client_accept_response {
    int ret;
    int eno;
} patty_client_accept_response;

typedef struct _patty_client_accept_message {
    int fd;
    patty_ax25_addr peer;
    char path[PATTY_AX25_SOCK_PATH_SIZE];
} patty_client_accept_message;

int patty_client_accept(patty_client *client,
                        int fd,
                        patty_ax25_addr *peer);

/*
 * connect()
 */
typedef struct _patty_client_connect_request {
    int fd;
    patty_ax25_addr peer;
} patty_client_connect_request;

typedef struct _patty_client_connect_response {
    int ret;
    int eno;
} patty_client_connect_response;

int patty_client_connect(patty_client *client,
                         int fd,
                         patty_ax25_addr *peer);

/*
 * close()
 */
typedef struct _patty_client_close_request {
    int fd;
} patty_client_close_request;

typedef struct _patty_client_close_response {
    int ret;
    int eno;
} patty_client_close_response;

int patty_client_close(patty_client *client,
                       int fd);

/*
 * sendto()
 */
ssize_t patty_client_sendto(patty_client *client,
                            int fd,
                            const void *buf,
                            size_t len,
                            patty_ax25_addr *addr);

/*
 * recvfrom()
 */
int patty_client_recvfrom(patty_client *client,
                          int fd,
                          void *buf,
                          size_t len,
                          patty_ax25_addr *addr);

#endif /* _PATTY_CLIENT_H */
