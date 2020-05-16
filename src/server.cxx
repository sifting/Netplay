#include "local.h"

Server::Server ()
{
}
Server::~Server ()
{
}

void
Server::handle_oob (const char *cmd, struct sockaddr_in *from)
{
	if (!strncmp ("reqc", cmd, 4))
	{	
		printf ("Challenge request...\n");
		/*Ensure the client hasn't already an active challenge*/
		uint64_t now = time_as_ms ();
		for (auto i = 0; i < MAX_CHALLENGES; i++)
		{
			Challenge *chal = challenges + i;
			if (now - chal->created >= CONNECTION_TIMEOUT)
			{
				continue;
			}
			if (memcmp (&chal->client, from, sizeof (chal->client)))
			{
				continue;
			}
			/*A valid one already exists*/
			return;
		}
		{/*Find a free challenge and send it to the prospect*/
			uint32_t i = 0;
			Challenge *nc = NULL;
			for (i = 0; i < MAX_CHALLENGES; i++)
			{
				nc = challenges + i;
				if (now - nc->created < CONNECTION_TIMEOUT)
				{
					continue;
				}
				goto Create;
			}
			/*No free challenges*/
			return;
		Create:
			/*Fill out the challenge*/
			std::random_device r;
			std::default_random_engine random (r ());
			uint32_t a = random ()&0xff;
			uint32_t b = random ()&0xff;
			uint32_t answer = a + b;

			nc->client = *from;
			nc->created = now;
			nc->expected = answer;

			/*Send the challenge back*/
			Network::instance ().send_oob (from, "chal %i %i %i", a, b, i);
		}
	}
	else if (!strncmp ("repl", cmd, 4))
	{
		printf ("Challenge reply...\n");
		steady_clock::time_point now = steady_clock::now ();
		uint32_t index = 0;
		uint32_t answer = 0;
		Challenge *chal = NULL;
		sscanf (cmd, "repl %i %i", &answer, &index);
		if (MAX_CHALLENGES <= index)
		{/*Tried to connect on the wrong challenge*/
			memset (chal, 0, sizeof (*chal));
			return;
		}
		chal = challenges + index;
		if (answer != chal->expected)
		{/*Invalidate the challenge on wrong reply*/
			memset (chal, 0, sizeof (*chal));
			return;
		}
		/*Create a new connection for the client*/
		if (!chal->answered)
		{
			client_add (from);
			chal->answered = true;
		}
		/*Let the user know that they may connect now*/
		Network::instance ().send_oob (from, "conn");
	}
	else if (!strncmp ("disc", cmd, 4))
	{
		printf ("Disconnecting client...\n");
		for (auto i = 0; i < _clients.size (); i++)
		{
			const struct sockaddr_in *addr = _clients[i]->con->remote ();
			if (!memcmp (addr, from, sizeof (*addr)))
			{
				client_remove (i);
				return;
			}
		}
	}
}

static void
cls_on_message (void *user, void *data, size_t size)
{
	auto cls = (Client_state *)user;
	{
		std::unique_lock<std::mutex> lock (cls->g);
		cls->messages.push_back (Net_buffer (data, size));
	}
}
void
Server::client_add (struct sockaddr_in *remote)
{
	auto cl = new Client_state;
	cl->state = CLS_CONNECTING;

	Connection::Callbacks cb;
	memset (&cb, 0, sizeof (cb));
	cb.user = cl;
	cb.on_message = cls_on_message;

	auto con = Network::instance ().connection_create (8192);
	con->callbacks_set (&cb);
	con->remote_from_addr (remote);
	cl->con = con;

	_clients.push_back (cl);
}
void
Server::client_remove (uint32_t index)
{
	auto cl = _clients[index];
	/*Remove from the client table*/
	size_t last = _clients.size () - 1;
	_clients[index] = _clients[last];
	_clients.resize (last);
	/*Free the resources*/
	Network::instance ().connection_destroy (cl->con);
	delete cl;
}
void
Server::frame ()
{
	for (auto &cl : _clients)
	{	/*Copy messages into local space*/
		std::vector<Net_buffer> messages;
		{
			std::unique_lock<std::mutex> lock (cl->g);
			messages = cl->messages;
			cl->messages.clear ();
		}
		/*Process the messages for the client*/
		for (auto &buffer : messages)
		{
			Message m (buffer._data, buffer._size);
			while (!m.eof ())
			{
				char command[32];
				m.read_raw (command, sizeof (command));
				printf ("CL command: %s\n", command);
				if (!strcmp ("userprops", command))
				{
					uint32_t count = m.read<uint32_t> ();
					while (count--)
					{
						char key[32];
						char val[32];
						m.read_raw (key, sizeof (key));
						m.read_raw (val, sizeof (val));
						cl->props[key] = val;
					}
					/*Print out the client properties*/
					printf ("Client properties:\n");
					for (auto &p : cl->props)
					{
						printf ("\t%s: %s\n", p.first.c_str (), p.second.c_str ());
					}
					continue;
				}
				/*The server sent a bad command!*/
				assert (0 && "Unknown command from client!");
				abort ();
			}
			buffer.release ();
		}
		/*Send any written data*/
		cl->con->send ();
	}
}
