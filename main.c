#include "common.h"
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>

int main(void)
{
	struct server *server = NULL;
	struct client *client = NULL;
	int sv[2];
	int pid;
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, sv) < 0) {
		error(0, errno, "failed to pair socket");
	}

	switch ((pid = fork())) {
	case 0:
		close(sv[0]);
		client = client_create(sv[1]);
		break;
	case -1:
		error(1, errno, "fork");
		break;
	default:
		close(sv[1]);
		server = server_create(sv[0]);
		break;
	}

	if (server)
		server_destroy(server);
	if (client)
		client_destroy(client);

	return 0;
}
