#include "local.h"

/*Singleton stuff. use init and destroy.*/
Network *Network::inst = NULL;
Network::Network ()
: _oob_handler (NULL)
{
	assert (NULL == Network::inst && "Only one network instance allowed");
	Network::inst = this;
}
Network::~Network ()
{
	Network::inst = NULL;
}

void
Network::thread_main ()
{
	char buf[NETWORK_PACKET_SIZE];
	struct sockaddr_in from;
	socklen_t from_size = sizeof (from);
	while (1)
	{	/*Wait for data*/
		ssize_t read = recvfrom (
			sock,
			buf, sizeof (buf),
			0,
			(struct sockaddr *)&from, &from_size
		);
		/*Ignore runty packets*/
		if (read < 4)
		{	/*Exit on error*/
			if (read < 0)
			{
				break;
			}
			continue;
		}
		/*Handle OOB messages*/
		if (!strncmp ("oob ", buf, 4))
		{
			const char *cmd = buf + 4;
			printf ("OOB: %s\n", cmd);
			if (_oob_handler)
			{
				_oob_handler (cmd, from);
			}
			continue;
		}
		/*Forward the packet to one of our connections, if any appropriate*/
		Connection *con = NULL;
		for (auto i = 0; i < _connections.size (); i++)
		{
			con = _connections[i];
			if (memcmp (&from, con->remote (), sizeof (from)))
			{
				continue;
			}
			goto Receive;
		}
		/*Must be random noise*/
		continue;
	Receive:
		con->receive (buf, read);
	}
}
void
Network::init ()
{	
#if _WIN32
	if (WSAStartup (MAKEWORD(2, 2), &wsa) != NO_ERROR)
	{
		abort ();
	}
#endif
	/*Create the UDP socket*/
	printf ("Creating socket...\n");
	sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		abort ();
	}
	/*Set socket to blocking mode*/
	printf ("Enabling blocking mode...\n");
	u_long blocking = 0;
	if (ioctlsocket (sock, FIONBIO, &blocking) != NO_ERROR)
	{
		abort ();
	}
	/*Bind it to the appropriate address*/
	uint16_t port = 27015;
	for (auto i = 0; i < 10; i++)
	{
		printf ("Binding socket to port %i...\n", port);
		struct sockaddr_in sa;
		memset (&sa, 0, sizeof (sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons (port++);
		sa.sin_addr.s_addr = htonl (INADDR_ANY);
		if (bind (sock, (struct sockaddr *)&sa, sizeof (sa)) < 0)
		{
			continue;
		}
		goto Done;
	}
	/*Failed to bind to a port*/
#if _WIN32
	closesocket (sock);
	WSACleanup ();
#else
	close (sock);
#endif
	abort ();
Done:
	/*Start the reader thread*/
	std::thread reader (&Network::thread_main, this);
	reader.detach ();
}
void
Network::destroy ()
{
	shutdown (sock, SD_BOTH);
#if _WIN32
	/*Close the socket*/
	closesocket (sock);
	WSACleanup ();
#else
	close (sock);
#endif
}
void
Network::send (const struct sockaddr_in *to, void *buf, size_t size)
{
	std::unique_lock<std::mutex> lock (_g);
	sendto (sock, (char *)buf, size, 0, (struct sockaddr *)to, sizeof (*to));
}
void
Network::send_oob (const struct sockaddr_in *to, const char *fmt, ...)
{
	char tmp[64] = "oob ";
	va_list args;
	
	va_start (args, fmt);
	vsnprintf (tmp + 4, sizeof (tmp) - 4, fmt, args);
	va_end (args);
	
	send (to, tmp, strlen (tmp) + 1);
}

Connection *
Network::connection_create (uint32_t recv_size)
{
	std::unique_lock<std::mutex> lock (_g);
	Connection *con = new Connection (recv_size);
	_connections.push_back (con);
	return con;
}
void
Network::connection_destroy (Connection *con)
{	
	std::unique_lock<std::mutex> lock (_g);
	/*Remove the connection from the list and free it*/
	auto len = _connections.size ();
	for (auto i = 0; i < len; i++)
	{
		Connection *c = _connections[i];
		if (c != con)
		{
			continue;
		}
		_connections[i] = _connections[--len];
		_connections.resize (len);
		delete con;
	}	
}

/*
**Connection
*/
void
Connection::reset ()
{
	_incoming = 0;
	_outgoing = 1;
	_loss = 0;
	
	/*Free any buffers hanging around before clearing*/
	for (auto &buffer : _queued)
	{
		delete [] buffer.data;
	}
	_queued.clear ();
	
	_send_data = NULL;
	_send_size = 0;
	_send_writ = 0;
	_send_frag = 0;
	
	_recv_read = 0;
	_recv_frag = 0;
}
void
Connection::send ()
{	/*Pull a message off the queue if none set, else there's no work*/
	if (NULL == _send_data)
	{
		if (0 == _queued.size ())
		{
			return;
		}
		/*Initialise the send state*/
		Buffer buf = _queued.back ();
		_send_data = buf.data;
		_send_size = buf.size;
		_send_writ = 0;
		_send_frag = 0;
		_queued.pop_back ();
	}
	/*Compute fragment size*/
	bool final = false;
	uint32_t length = _send_size - _send_writ;
	/*Compose the packet and send it*/
	uint8_t packet[NETWORK_PACKET_SIZE];
	Message m (packet, sizeof (packet));
	if (length <= SEND_SIZE)
	{
		m.write<uint64_t> (_outgoing++);
		m.write<uint16_t> (_send_frag|FRAG_FINAL);
		final = true;
	}
	else
	{
		m.write<uint64_t> (_outgoing);
		m.write<uint16_t> (_send_frag++);
		length = SEND_SIZE;
	}	
	m.write_raw (_send_data + _send_writ, length);
	Network::instance ().send (&_remote, m.data (), m.length ());
	/*Handle end of message stuff*/
	if (final)
	{	/*Free the message and mark it as null,
		so that the next one may be sent*/
		delete [] _send_data;
		_send_data = NULL;
		/*Notify the user if they requested it*/
		if (_cb.on_sent != NULL)
		{
			_cb.on_sent (_cb.user);
		}
	}
}
void
Connection::receive (void *buf, size_t size)
{
	Message m (buf, size);
	/*Ignore message if it's old*/
	uint64_t id = m.read<uint64_t> ();
	if (id <= _incoming)
	{
		printf ("ignoring old message (%i <= %i)\n", id, _incoming);
		return;
	}
	/*Reset the read state if we dropped the last message*/
	uint32_t loss = (id - 1) - _incoming;
	if (loss)
	{
		_recv_read = 0;
		_recv_frag = 0;
		_loss += loss;
		printf ("loss %i\n", _loss);
	}
	/*Read fragment sequence and extract finality bit*/
	uint32_t frag = m.read<uint16_t> ();
	bool final = (frag&FRAG_FINAL) != 0;
	frag &= ~FRAG_FINAL;
	/*Reject out of order fragments*/
	if (frag != _recv_frag)
	{/*NB: as it stands,
	this will also result in the message being dropped*/
		printf ("out of order fragment (%i != %i)\n", frag, _recv_frag);
		return;
	}
	_recv_frag = frag;
	/*Append to receive buffer*/
	uint32_t length = _recv_size - _recv_read;
	uint32_t read = m.read_raw (_recv_data + _recv_read, length);
	_recv_read += read;
	/*Turn the message over to the user for processing,
	if we have received the whole thing*/
	if (final)
	{
		printf ("cool message received\n");
		if (_cb.on_message != NULL)
		{
			_cb.on_message (_cb.user, _recv_data, _recv_read);
		}
		_incoming = id;
		_recv_read = 0;
		_recv_frag = 0;
	}
}
