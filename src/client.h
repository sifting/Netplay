#pragma once

#include <unordered_map>
#include <mutex>

#define CS_DISCONNECTED	0
#define CS_REQUESTING	1
#define CS_CHALLENGING	2
#define CS_CONNECTING	3
#define CS_CONNECTED	4

class Client
{
public:
	Client ();
	virtual ~Client ();
	
	void request (const char *remote, uint16_t port);
	void disconnect (void);
	
	void handle_oob (const char *cmd, struct sockaddr_in *from);
	void queue_message (void *data, size_t size);

	void frame ();
	
	void property_set (string key, const char *value);
	const char *property_get (string key);

private:	
	void send_properties ();
	
private:
	uint32_t _state;
	Connection *_con;
	std::unordered_map<string, string> _properties;
	std::vector<Net_buffer> _messages;
	std::mutex _g;
};
