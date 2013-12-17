#include "common.h"
#include <pthread.h>

int main(void)
{
	struct client *client;

	client = client_create();

	client_destroy(client);

	return 0;
}
