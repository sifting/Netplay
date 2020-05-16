#pragma once

#ifdef _WIN32
#	include <winsock2.h>
typedef SOCKET Socket;
typedef int socklen_t;
typedef long long ssize_t;
#else
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
typedef int Socket;
#endif

#include <random>
#include <thread>
#include <mutex>
#include <vector>

#include "utils.h"

class Connection;
class Network;

struct Net_buffer
{
	uint8_t *_data;
	size_t _size;
	Net_buffer (void *data, size_t size)
	{
		_data = new uint8_t[size];
		memcpy (_data, data, size);
		_size = size;
	}
	void release ()
	{
		delete [] _data;
		_data = NULL;
		_size = 0;
	}
};

#define NETWORK_PACKET_SIZE	1500
typedef void (*Network_OOB) (const char *cmd, struct sockaddr_in from);
class Network
{
public:
	static Network *inst;
	Network ();
	virtual ~Network ();
	static Network &instance () { return *Network::inst; };
	
	void oob_set (Network_OOB oob) { _oob_handler = oob; };
	
	void init ();
	void destroy ();
	
	void send (const struct sockaddr_in *to, void *buf, size_t size);
	void send_oob (const struct sockaddr_in *to, const char *fmt, ...);
	
	Connection *connection_create (uint32_t recv_size);
	void connection_destroy (Connection *con);
private:
	Network_OOB _oob_handler;
	void thread_main ();
	Socket sock;
	std::vector<Connection *> _connections;
	std::mutex _g;
#if _WIN32
	/*Is this even needed?*/
	WSADATA wsa;
#endif
};

#define CONNECTION_TIMEOUT	30000	/*miliseconds*/
#define SEND_SIZE			1024	/*Must be < NETWORK_PACKET_SIZE*/
#define FRAG_FINAL			0x8000
class Connection
{
public:
	struct Callbacks
	{
		void *user;
		void (*on_sent) (void *user);
		void (*on_message) (void *user, void *data, size_t buf);
	};
	Connection (uint32_t recv_size)
	{
		_recv_data = new uint8_t[recv_size];
		_recv_size = recv_size;
		reset ();
	}
	virtual ~Connection ()
	{
		delete [] _recv_data;
	}
	
	void callbacks_set (Callbacks *cb) { _cb = *cb; }
	
	const struct sockaddr_in *remote () { return &_remote; }
	void remote_from_ip_port (const char *ip, uint16_t port)
	{
		_remote.sin_addr.s_addr = inet_addr (ip);
		_remote.sin_port = htons (port);
		_remote.sin_family = AF_INET;
	}
	void remote_from_addr (struct sockaddr_in *addr)
	{
		_remote = *addr;
	}
	
	void write (const void *buf, uint32_t size)
	{
		Buffer b;
		b.size = size;
		b.data = new uint8_t[size];
		memcpy (b.data, buf, size);
		_queued.push_back (b);
	};
	size_t queued ()
	{
		return _queued.size ();
	}
	
	void reset ();
	void send ();
	void receive (void *buf, size_t size);
	
private:
	struct Buffer
	{
		size_t size;
		uint8_t *data;
	};
	struct sockaddr_in _remote;
	Callbacks _cb;
	uint64_t _incoming;
	uint64_t _outgoing;
	uint32_t _loss;
	
	std::vector<Buffer> _queued;
	uint8_t *_send_data;
	uint32_t _send_size;
	uint32_t _send_writ;
	uint32_t _send_frag;
	
	uint8_t *_recv_data;
	uint32_t _recv_size;
	uint32_t _recv_read;
	uint32_t _recv_frag;
};

