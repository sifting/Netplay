#include "local.h"

static Client *client = NULL;
static Server *server = NULL;

static void
oob_handler (const char *cmd, struct sockaddr_in from)
{
	if (server) server->handle_oob (cmd, &from);
	else client->handle_oob (cmd, &from);
}

static void
exit_handler (void)
{
	/*Tear down everything*/
	if (client)
	{
		delete client;
		client = NULL;
	}
	if (server)
	{
		delete server;
		server = NULL;
	}
	Network::instance ().destroy ();
}

int
main (int argc, char **argv)
{	/*Parse arguments*/
	char remote[32] = "127.0.0.1";
	char nick[32] = "user";
	char pass[32] = "none";
	uint16_t port = 27015;
	bool dedicated = false;
	for (auto i = 1; i < argc; i++)
	{
		char *p = argv[i];
		/*Check to see if it's a valid argument*/
		if (*p++ != '-')
		{
			i++;
			continue;
		}
		/*Parse out arguments*/
		if (!strcmp ("remote", p))
		{
			snprintf (remote, sizeof (remote), "%s", argv[++i]);
		}
		else if (!strcmp ("port", p))
		{
			port = atoi (argv[++i]);
		}
		else if (!strcmp ("dedicated", p))
		{
			dedicated = true;
		}
		else if (!strcmp ("nick", p))
		{
			snprintf (nick, sizeof (nick), "%s", argv[++i]);
		}
		else if (!strcmp ("pass", p))
		{
			snprintf (pass, sizeof (pass), "%s", argv[++i]);
		}
	}
	
	printf ("remote: %s\n", remote);
	printf ("port %i\n", port);
	printf ("dedicated: %s\n", ((const char *[]){"no", "yes"})[dedicated]);
	printf ("nickname: %s\n", nick);
	printf ("password: %s\n", pass);

	/*Start the network service*/
	new Network;
	Network::instance ().init ();
	Network::instance ().oob_set (oob_handler);
	/*Start the client and server as appropriate*/
	if (!dedicated)
	{	/*Spawn the server*/
		if (!strcmp ("localhost", remote))
		{
			server = new Server;
		}
		client = new Client;
		client->property_set ("nickname", nick);
		client->property_set ("password", pass);
		client->request (remote, port);
	}
	else
	{
		printf ("Dedicated: suppressing client start up\n");
		server = new Server;
	}
	
	atexit (exit_handler);
	while (1)
	{
		if (server) server->frame ();
		if (client) client->frame ();
		/*Wait a little bit*/
		std::this_thread::sleep_for (std::chrono::seconds (1));
	}
	return EXIT_SUCCESS;
}
