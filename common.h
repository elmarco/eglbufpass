#ifndef COMMON_H
#define COMMON_H

struct server;

struct bufinfo {
	int id;
	int width, height;
	int stride;
	int format;
};

struct server *server_create(int sock_fd);
void server_destroy(struct server *server);

struct client;
struct client *client_create(int sock_fd);
void client_destroy(struct client *client);

#endif
