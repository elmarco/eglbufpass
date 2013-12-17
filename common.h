#ifndef COMMON_H
#define COMMON_H

struct server;

struct server *server_create(void);
void server_destroy(struct server *server);

struct client;
struct client *client_create(void);
void client_destroy(struct client *client);

#endif
