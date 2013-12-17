#ifndef COMMON_H
#define COMMON_H

struct server;

struct update_dirty_rect {
	int id;
	int x, y;
	int width, height;
};

struct bufinfo {
	int id;
	int width, height;
	int stride;
	int format;
};

#define CMD_TYPE_BUF 1
#define CMD_TYPE_DIRT 2
struct cmd_buf {
	int type;
	union cmds {
		struct update_dirty_rect dirty;
		struct bufinfo buf;
	} u;
};

struct server *server_create(int sock_fd);
void server_destroy(struct server *server);

struct client;
struct client *client_create(int sock_fd);
void client_destroy(struct client *client);

#endif
