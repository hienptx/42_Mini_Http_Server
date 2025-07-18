#pragma once

#include <string>
#include <vector>
#include <poll.h>
#include <unordered_map>
#include <map>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <set>

#include "../includes/Request.hpp"
#include "../includes/Response.hpp"
#include "../includes/Utils.hpp"
#include "../includes/Config_Manager.hpp"

#define BUF_SIZE 8194

struct ClientSession {
	std::string buffer;
	bool headers_received = false;
	int content_length = 0;
};

// Add this struct to track CGI process state
struct CGIState {
  pid_t pid;              // CGI process ID
  int stdin_fd;           // Pipe for writing to CGI stdin
  int stdout_fd;          // Pipe for reading from CGI stdout
  std::string input_buffer; // Request body to send to CGI
  std::string output_buffer; // Accumulated CGI output
  int client_fd;          // Associated client file descriptor
  bool done;              // Whether CGI has finished
};


class Server {
	private:
		std::vector<ServerConfig> config;
		std::vector<int> ss_Fds;
		std::map<int, ServerConfig> serverSockets;
		std::map<int, ServerConfig> clientConfigs;
		std::vector<int> uniqPorts;
		std::map<int, ClientSession> client_sessions;
		std::set<int> seenPorts;

	public:
		static std::vector<struct pollfd> poll_fds;
		static std::map<int, CGIState> cgi_states; // Keyed by stdout_fd
		static int current_client_fd; // Set in main loop before handling request
		static bool running;
		std::unordered_map<int, std::string> responses;
		
		Server(std::vector<ServerConfig> config);
		~Server();

		//initial step when we go through the results of the parser,
		//look for the unique ports and create socket for each port we gonna listen on
		void setupPorts();
		void run();
		static void signalHandler(int signum);
		void mainLoop();
		void handleCGIPipeEvents(size_t i);
		void handleSocketEvents(size_t i);
		void cleanup();

		void handleNewConnection(int listen_id);
		void handleClientData(int client_fd);
		void handleClientWrite(int client_fd);
		void closeClient(int client_fd);
		
		
		// Handling client data
		std::string getHostFromHeaders(const std::string& headers);
		const ServerConfig* getServerConfigByHost(const std::vector<ServerConfig>& configs,
													const std::string& host, int port);
		int getListeningPortForClient(int client_fd);
		bool receiveData(int client_fd);
		bool processHeaders(ClientSession& session);
		bool isFullRequestReceived(const ClientSession& session);
		void processRequest(int client_fd, ClientSession& session);
		void enableWriteEvents(int client_fd);
		std::string processCGIOutput(const std::string& output);
};

