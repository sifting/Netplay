#pragma once

#include <unordered_map>
#include <vector>
#include <mutex>

#define MAX_CHALLENGES 32

#define CLS_CONNECTING		0
#define CLS_CONNECTED		1
#define CLS_DISCONNECTING	2
#define CLS_DISCONNECTED	3
struct Client_state
{
	uint32_t state;
	Connection *con;
	std::unordered_map<string, string> props;
	std::vector<Net_buffer> messages;
	std::mutex g;
};
	
class Server
{
public:
	Server ();
	virtual ~Server ();
	
	void handle_oob (const char *cmd, struct sockaddr_in *from);
	void frame ();

private:
	struct Challenge
	{
		struct sockaddr_in client;
		uint64_t created;
		uint32_t expected;
		bool answered;
	};
	
	void client_add (struct sockaddr_in *remote);
	void client_remove (uint32_t index);
	
	Challenge challenges[MAX_CHALLENGES];
	std::vector<Client_state *> _clients;
	std::mutex _g;
};
