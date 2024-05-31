#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <patty/ax25.h>
#include <patty/daemon.h>
#include <patty/util.h>

struct _patty_client_sock {
    int fd;
    char path[PATTY_AX25_SOCK_PATH_SIZE];
};

struct _patty_client {
    int fd;
    patty_dict *socks;
};

static const char *find_sock(const char *path) {
    struct stat st;

    return path?
           path: stat(PATTY_CLIENT_DEFAULT_SOCK_NAME, &st) == 0?
                      PATTY_CLIENT_DEFAULT_SOCK_NAME:
                      PATTY_DAEMON_DEFAULT_SOCK;
}

patty_client *patty_client_new(const char *path) {
    patty_client *client;
    struct sockaddr_un addr;

    if ((client = malloc(sizeof(*client))) == NULL) {
        goto error_malloc_client;
    }

    if ((client->socks = patty_dict_new()) == NULL) {
        goto error_dict_new;
    }

    if ((client->fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
        goto error_socket;
    }

    memset(&addr, '\0', sizeof(addr));
    addr.sun_family = AF_UNIX;

    patty_strlcpy(addr.sun_path, find_sock(path), sizeof(addr.sun_path));

    if (connect(client->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        goto error_connect;
    }

    return client;

error_connect:
    close(client->fd);

error_socket:
    patty_dict_destroy(client->socks);

error_dict_new:
    free(client);

error_malloc_client:
    return NULL;
}

ssize_t patty_client_read(patty_client *client, void *buf, size_t len) {
    return read(client->fd, buf, len);
}

ssize_t patty_client_write(patty_client *client, const void *buf, size_t len) {
    return write(client->fd, buf, len);
}

static int request_close(patty_client *client,
                         int fd) {
    enum patty_client_call call = PATTY_CLIENT_CLOSE;

    patty_client_close_request request = { fd };
    patty_client_close_response response;

    size_t len;

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if ((len = read(client->fd, &response, sizeof(response))) < 0) {
        goto error_io;
    } else if (len != sizeof(response)) {
        errno = EIO;

        goto error_io;
    }

    errno = response.eno;

    return response.ret;

error_io:
    return -1;
}

static int destroy_sock(uint32_t key, void *value, void *ctx) {
    patty_client *client    = ctx;
    patty_client_sock *sock = value;

    (void)request_close(client, sock->fd);

    free(sock);

    return 0;
}

void patty_client_destroy(patty_client *client) {
    close(client->fd);

    (void)patty_dict_each(client->socks, destroy_sock, client);
    patty_dict_destroy(client->socks);

    free(client);
}

int patty_client_ping(patty_client *client) {
    int call = PATTY_CLIENT_PING,
        pong;

    ssize_t len;

    if ((len = write(client->fd, &call, sizeof(call))) < 0 || len == 0) {
        goto done;
    }

    if ((len = read(client->fd, &pong, sizeof(pong))) < 0 || len == 0) {
        goto done;
    }

    return pong;

done:
    if (errno == EIO) {
        errno = 0;

        return 0;
    }

    return -1;
}

int patty_client_socket(patty_client *client,
                        int proto,
                        int type) {
    enum patty_client_call call = PATTY_CLIENT_SOCKET;

    patty_client_socket_request request = {
        proto, type
    };

    patty_client_socket_response response;
    patty_client_sock *sock;

    int fd;
    struct termios t;

    if ((sock = malloc(sizeof(*sock))) == NULL) {
        goto error_malloc_sock;
    }

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if (read(client->fd, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    sock->fd = response.fd;

    patty_strlcpy(sock->path, response.path, sizeof(sock->path));

    if ((fd = open(sock->path, O_RDWR)) < 0) {
        goto error_open;
    }

    if (tcgetattr(fd, &t) < 0) {
        goto error_tcgetattr;
    }

    cfmakeraw(&t);

    if (tcsetattr(fd, TCSANOW, &t) < 0) {
        goto error_tcsetattr;
    }

    if (patty_dict_set(client->socks,
                       (uint32_t)fd,
                       sock) == NULL) {
        goto error_dict_set;
    }

    errno = response.eno;

    return fd;

error_dict_set:
error_tcsetattr:
error_tcgetattr:
    (void)close(fd);

error_open:
    (void)request_close(client, sock->fd);

error_io:
    free(sock);

error_malloc_sock:
    return -1;
}

int patty_client_setsockopt(patty_client *client,
                            int fd,
                            int opt,
                            void *data,
                            size_t len) {
    enum patty_client_call call = PATTY_CLIENT_SETSOCKOPT;

    patty_client_setsockopt_request request = {
        .opt = opt,
        .len = len
    };

    patty_client_setsockopt_response response;

    patty_client_sock *sock;

    if ((sock = patty_dict_get(client->socks, (uint32_t)fd)) == NULL) {
        errno = EBADF;

        goto error_dict_get;
    }

    request.fd = sock->fd;

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if (write(client->fd, data, len) < 0) {
        goto error_io;
    }

    if (read(client->fd, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    errno = response.eno;

    return response.ret;

error_io:
error_dict_get:
    return -1;
}

int patty_client_bind(patty_client *client,
                      int fd,
                      patty_ax25_addr *addr) {
    enum patty_client_call call = PATTY_CLIENT_BIND;

    patty_client_bind_request request;
    patty_client_bind_response response;

    patty_client_sock *sock;

    if ((sock = patty_dict_get(client->socks, (uint32_t)fd)) == NULL) {
        errno = EBADF;

        goto error_dict_get;
    }

    memset(&request, '\0', sizeof(request));

    request.fd = sock->fd;

    memcpy(&request.addr, addr, sizeof(*addr));

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if (read(client->fd, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    errno = response.eno;

    return response.ret;

error_io:
error_dict_get:
    return -1;
}

int patty_client_listen(patty_client *client,
                        int fd) {
    enum patty_client_call call = PATTY_CLIENT_LISTEN;

    patty_client_listen_request request;
    patty_client_listen_response response;

    patty_client_sock *sock;

    if ((sock = patty_dict_get(client->socks, (uint32_t)fd)) == NULL) {
        errno = EBADF;

        goto error_dict_get;
    }

    request.fd = sock->fd;

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if (read(client->fd, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    errno = response.eno;

    return response.ret;

error_io:
error_dict_get:
    return -1;
}

int patty_client_accept(patty_client *client,
                        int fd,
                        patty_ax25_addr *peer) {
    enum patty_client_call call = PATTY_CLIENT_ACCEPT;

    patty_client_accept_request request;
    patty_client_accept_response response;
    patty_client_accept_message message;

    patty_client_sock *local,
                      *remote;

    int pty;
    struct termios t;

    if ((local = patty_dict_get(client->socks, (uint32_t)fd)) == NULL) {
        errno = EBADF;

        goto error_dict_get;
    }

    if ((remote = malloc(sizeof(*remote))) == NULL) {
        goto error_malloc_remote;
    }

    request.fd = local->fd;

    memset(&response, '\0', sizeof(response));

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    /*
     * First, the server will tell us if the fd specified in accept() is indeed
     * accepting connections.
     */
    if (read(client->fd, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    if (response.ret < 0) {
        errno = response.eno;

        return response.ret;
    }

    /*
     * Next, we will wait for the server to receive a SABM or SABME frame, and
     * notify us via the listening socket that a connection has been accepted.
     */
    if (read(fd, &message, sizeof(message)) < 0) {
        goto error_io;
    }

    remote->fd = message.fd;

    memcpy(peer, &message.peer, sizeof(*peer));
    patty_strlcpy(remote->path, message.path, sizeof(remote->path));

    if ((pty = open(remote->path, O_RDWR)) < 0) {
        goto error_open;
    }

    if (tcgetattr(pty, &t) < 0) {
        goto error_tcgetattr;
    }

    cfmakeraw(&t);

    if (tcsetattr(pty, TCSANOW, &t) < 0) {
        goto error_tcsetattr;
    }

    if (patty_dict_set(client->socks,
                       (uint32_t)pty,
                       remote) == NULL) {
        goto error_dict_set;
    }

    errno = response.eno;

    return pty;

error_tcsetattr:
error_tcgetattr:
error_dict_set:
    (void)close(pty);

error_open:
    (void)request_close(client, remote->fd);

error_io:
    free(remote);

error_malloc_remote:
error_dict_get:
    return -1;
}

int patty_client_connect(patty_client *client,
                         int fd,
                         patty_ax25_addr *peer) {
    enum patty_client_call call = PATTY_CLIENT_CONNECT;

    patty_client_connect_request request;
    patty_client_connect_response response;

    patty_client_sock *sock;

    if ((sock = patty_dict_get(client->socks, (uint32_t)fd)) == NULL) {
        errno = EBADF;

        goto error_dict_get;
    }

    memset(&request, '\0', sizeof(request));

    request.fd = sock->fd;

    memcpy(&request.peer, peer, sizeof(*peer));

    if (write(client->fd, &call, sizeof(call)) < 0) {
        goto error_io;
    }

    if (write(client->fd, &request, sizeof(request)) < 0) {
        goto error_io;
    }

    if (read(client->fd, &response, sizeof(response)) < 0) {
        goto error_io;
    }

    errno = response.eno;

    return response.ret;

error_io:
error_dict_get:
    return -1;
}

int patty_client_close(patty_client *client,
                       int fd) {
    patty_client_sock *sock;

    if ((sock = patty_dict_get(client->socks, (uint32_t)fd)) == NULL) {
        errno = EBADF;

        goto error_dict_get;
    }

    if (request_close(client, sock->fd) < 0) {
        goto error_request_close;
    }

    if (close(fd) < 0) {
        goto error_close;
    }

    patty_dict_delete(client->socks, (uint32_t)fd);

    free(sock);

    return 0;

error_close:
error_request_close:
error_dict_get:
    return -1;
}
