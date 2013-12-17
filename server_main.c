#include "common.h"
#include <pthread.h>

int main(void)
{
	struct server *server;
	struct client *client;
	server = server_create();

	server_destroy(server);
	return 0;
}
