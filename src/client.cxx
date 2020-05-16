#include "local.h"

static void
on_message (void *user, void *data, size_t size)
{
	auto cl = static_cast<Client *>(user);
	cl->queue_message (data, size);
}
static void
on_sent (void *user)
{
	printf ("sent a message out\n");
}

Client::Client ()
: _state (CS_DISCONNECTED)
{
	Connection::Callbacks cb;
	memset (&cb, 0, sizeof (cb));
	cb.user = this;
	cb.on_message = on_message;
	cb.on_sent = on_sent;
	
	/*Create a connection with an arbitrary receive buffer size*/
	_con = Network::instance ().connection_create (8192);
	_con->callbacks_set (&cb);
}
Client::~Client ()
{
	if (_con)
	{
		Network::instance ().connection_destroy (_con);
		_con = NULL;
	}
}

void
Client::request (const char *remote, uint16_t port)
{
	if (CS_DISCONNECTED < _state)
	{
		disconnect ();
	}
	_con->remote_from_ip_port (remote, port);
	_state = CS_REQUESTING;
}
void
Client::disconnect (void)
{	/*Send the message a few times in case one drops*/
	for (auto i = 0; i < 3; i++)
	{
		Network::instance ().send_oob (_con->remote (), "disc");
	}
	_state = CS_DISCONNECTED;
}
void
Client::handle_oob (const char *cmd, struct sockaddr_in *from)
{
	if (!strncmp ("chal", cmd, 4))
	{	/*Ignore if we're not requesting a challenge*/
		if (_state != CS_REQUESTING)
		{
			return;
		}
		printf ("Challenging...\n");
		uint32_t a = 0;
		uint32_t b = 0;
		uint32_t index = 0;
		sscanf (cmd, "chal %i %i %i", &a, &b, &index);
		Network::instance ().send_oob (from, "repl %i %i", a + b, index);
		_state = CS_CHALLENGING;
	}
	else if (!strncmp ("conn", cmd, 4))
	{	/*Ignore if we're not challenging*/
		if (_state != CS_CHALLENGING)
		{
			return;
		}
		_con->reset ();
		_state = CS_CONNECTED;
		/*Send the properties*/
		send_properties ();
	}
	else if (!strncmp ("disc", cmd, 4))
	{	
		_state = CS_DISCONNECTED;
	}
}
void
Client::queue_message (void *data, size_t size)
{
	std::unique_lock<std::mutex> lock (_g);
	_messages.push_back (Net_buffer (data, size));
}
void
Client::property_set (string key, const char *value)
{
	auto it = _properties.find (key);
	if (_properties.end () != it)
	{	/*NULL values unset the key*/
		if (NULL == value)
		{
			_properties.erase (it);
			return;
		}
	}
	_properties[key] = value;
}
const char *
Client::property_get (string key)
{
	auto it = _properties.find (key);
	if (_properties.end () == it)
	{
		return NULL;
	}
	return it->second.c_str ();
}
void
Client::send_properties ()
{
	uint8_t packet[NETWORK_PACKET_SIZE];
	Message m (packet, sizeof (packet));
	/*Package up the user properties into a message*/
	const char *cmd = "userprops";
	m.write_raw (cmd, strlen (cmd) + 1);
	m.write<uint32_t> (_properties.size ());
	for (auto &prop : _properties)
	{
		const char *key = prop.first.c_str ();
		const char *val = prop.second.c_str ();
		m.write_raw (key, strlen (key) + 1);
		m.write_raw (val, strlen (val) + 1);
	}
	/*Queue the message*/
	_con->write (m.data (), m.length ());
}
void
Client::frame ()
{	/*Emit OOB packets*/
	switch (_state)
	{
	case CS_REQUESTING:
		printf ("Requesting Challenge...\n");
		Network::instance ().send_oob (_con->remote (), "reqc");
		break;
	default:
		break;
	}
	/*Copy the messages into local space*/
	std::vector<Net_buffer> messages;
	{
		std::unique_lock<std::mutex> lock (_g);
		messages = _messages;
		_messages.clear ();
	}
	/*Process messages*/
	for (auto &buffer : messages)
	{
		Message m (buffer._data, buffer._size);
		while (!m.eof ())
		{
			char command[32];
			m.read_raw (command, sizeof (command));
			printf ("SV command: %s\n", command);
			/*Process server commands*/
		}
		buffer.release ();
	}
	/*Send any data written to the socket*/
	_con->send ();
}
