#include "../includes/Server.hpp"

std::vector<struct pollfd> Server::poll_fds;
std::map<int, CGIState> Server::cgi_states;
int Server::current_client_fd = -1;

volatile sig_atomic_t gSignal = 1;

bool Server::running = true;

Server::Server(std::vector<ServerConfig> config) : config(config) {
	setupPorts();
}

Server::~Server() {
}

void Server::run() {
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	mainLoop();
	cleanup();
}

void Server::signalHandler(int signum) {
	(void)signum;
	running = false;
}

void stopLoop(int) {
	gSignal = 0;
}

void Server::mainLoop() {
    while (running) {
        int poll_count = poll(poll_fds.data(), poll_fds.size(), 1000);
        if (poll_count < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        for (size_t i = 0; i < poll_fds.size(); i++) {
            int fd = poll_fds[i].fd;

            // Skip if no events
            if (poll_fds[i].revents == 0)
                continue;

            // Check if it's a CGI pipe
            if (cgi_states.find(fd) != cgi_states.end()) {
                handleCGIPipeEvents(i);
                continue;
            }

            // Otherwise, handle normal socket events
            handleSocketEvents(i);
        }
    }
}

void Server::handleCGIPipeEvents(size_t i) {
    int fd = poll_fds[i].fd;
    auto cgi_it = cgi_states.find(fd);
    if (cgi_it == cgi_states.end())
        return;

    // Handle reading from CGI stdout
    if (poll_fds[i].revents & POLLIN) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n > 0) {
            cgi_it->second.output_buffer.append(buf, n);
        } else if (n == 0) {
            int client_fd = cgi_it->second.client_fd;

            // Check if client still exists
            bool client_exists = false;
            for (const auto& pfd : poll_fds) {
                if (pfd.fd == client_fd) {
                    client_exists = true;
                    break;
                }
            }

            if (client_exists) {
                std::string response = processCGIOutput(cgi_it->second.output_buffer);
                responses[client_fd] = response;

                for (auto& pfd : poll_fds) {
                    if (pfd.fd == client_fd) {
                        pfd.events = POLLOUT;
                        pfd.revents = 0;
                        break;
                    }
                }
            }

            if (cgi_it->second.stdin_fd > 0)
                close(cgi_it->second.stdin_fd);
            close(fd);
            waitpid(cgi_it->second.pid, NULL, 0);

            cgi_states.erase(fd);
            poll_fds.erase(poll_fds.begin() + i);
            i--;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read CGI pipe");
        }
        return;
    }

    // Handle writing to CGI stdin
    if (fd == cgi_it->second.stdin_fd && (poll_fds[i].revents & POLLOUT)) {
        std::string& input = cgi_it->second.input_buffer;
        if (!input.empty()) {
            ssize_t n = write(fd, input.c_str(), input.size());
            if (n > 0) {
                input.erase(0, n);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("write CGI pipe");
            }
        }

        if (input.empty()) {
            close(fd);
            cgi_it->second.stdin_fd = -1;
            poll_fds.erase(poll_fds.begin() + i);
            i--;
        }
        return;
    }
}

void Server::handleSocketEvents(size_t i) {
    int fd = poll_fds[i].fd;

    if (poll_fds[i].revents & POLLIN) {
        if (std::find(ss_Fds.begin(), ss_Fds.end(), fd) != ss_Fds.end()) {
            handleNewConnection(fd);
        } else {
            handleClientData(fd);
        }
    } else if (poll_fds[i].revents & POLLOUT) {
        handleClientWrite(fd);
    }
}

void Server::handleNewConnection(int listen_id) {
	// std::cout << "DEBUG: handleNewConnection" << std::endl;
	int client_fd = accept(listen_id, nullptr, nullptr);
		if (client_fd < 0) {
			perror("accept");
			return ;
		}
	struct pollfd pfd = {client_fd, POLLIN, 0};
	poll_fds.push_back(pfd);
	clientConfigs[client_fd] = serverSockets[listen_id];
}

void Server::handleClientData(int client_fd) {
    current_client_fd = client_fd;
    // std::cout << "DEBUG: handleClientData" << std::endl;

    if (!receiveData(client_fd)) {
        closeClient(client_fd);
        return;
    }
    ClientSession& session = client_sessions[client_fd];
    if (!processHeaders(session))
        return; // wait for more data

    if (isFullRequestReceived(session))
        processRequest(client_fd, session);
}

void Server::handleClientWrite(int client_fd) {
	// std::cout << "DEBUG: handleClientWrite" << std::endl;
	auto it = responses.find(client_fd);
	if (it == responses.end()) {
		std::cerr << "No response found for client " << client_fd << std::endl;
		return ;
	}
	const std::string& response = it->second;
  if (response.empty()) {
    // For CGI requests that return empty responses, keep the connection open
    responses.erase(client_fd);
    
    // Reset to POLLIN to allow for further requests
    for (auto& pfd : poll_fds) {
        if (pfd.fd == client_fd) {
            pfd.events = POLLIN;
            pfd.revents = 0;
            break;
        }
    }
    return;
  }
	ssize_t bytes_sent = send(client_fd, response.c_str(), response.length(), 0);
	if (bytes_sent <= 0) {
		perror("send");
		closeClient(client_fd);
		responses.erase(client_fd);
		return ;
	}
	// std::cout << "Sent response to client :\n" << response << std::endl;
	responses.erase(client_fd);

	 // Check if this client is waiting for CGI response
    bool is_cgi_client = false;
    for (const auto& pair : cgi_states) {
        if (pair.second.client_fd == client_fd) {
            is_cgi_client = true;
            break;
        }
    }
    
    // Only close the client if it's not waiting for CGI output
    if (!is_cgi_client) {
        closeClient(client_fd);
    } else {
        // For CGI clients, reset to POLLIN for possible future data
        for (auto& pfd : poll_fds) {
            if (pfd.fd == client_fd) {
                pfd.events = POLLIN;
                pfd.revents = 0;
                break;
            }
        }
    }
}

void Server::closeClient(int client_fd){
	close (client_fd);

	for (std::vector<struct pollfd>::iterator it = poll_fds.begin(); it != poll_fds.end(); ++it) {
		if (it->fd == client_fd) {
			poll_fds.erase(it);
			break;
		}
	}
	clientConfigs.erase(client_fd);
}

void Server::cleanup () {
	for (size_t i = 0; i < poll_fds.size(); i++) {
		close(poll_fds[i].fd);
	}
	poll_fds.clear();
}

void Server::setupPorts() {
	std::set<int> seenPorts;
	for (std::vector<ServerConfig>::iterator it = config.begin(); it != config.end(); ++it) {
		if (!seenPorts.insert(it->port).second) {
			std::cerr << "Error: Port " << it->port << " is declared more than once in the config.\n";
			exit(1);
		}
	}

	std::vector<ServerConfig>::iterator it = config.begin();
	std::vector<ServerConfig>::iterator it_end = config.end();

	while (it != it_end) {
		if(std::find(uniqPorts.begin(), uniqPorts.end(), it->port) == uniqPorts.end()) {

			it->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
			if(it->sock_fd  < 0) {
				std::cerr << "Failed to create socket\n";
				exit(1);
			}
			int opt = 1;
			if (setsockopt(it->sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
				perror("setsockopt");
				exit(1);
			}

			if (fcntl(it->sock_fd, F_SETFL, O_NONBLOCK) < 0) {
				perror("fcntl");
				exit(1);
			}

			sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = INADDR_ANY;
			addr.sin_port = htons(it->port);

			if (bind(it->sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
				std::cerr << "Bind failed\n";
				return ;
			}

			if (listen(it->sock_fd, SOMAXCONN) < 0) {
				std::cerr << "Listen failed\n";
				return ;
			}
			std::cout << "Middle Serv running on the port " << it->port << std::endl;
			struct pollfd pollfd = {it->sock_fd, POLLIN, 0};
			poll_fds.push_back(pollfd);
			ss_Fds.push_back(it->sock_fd);
			uniqPorts.push_back(it->port);
			serverSockets[it->sock_fd] = *it; //I added the server socket to map
			// std::cout << "we pushed: "  << "sock_fd: " << it->sock_fd << ", port: " << it->port << std::endl;
		}
		++it;
	}
}

std::string Server::processCGIOutput(const std::string& output) {
    size_t header_end = output.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        // No headers, assume HTML content
        Response res(config);
        return res.buildResponse(output, 200, "text/html");
    }

    std::string headers = output.substr(0, header_end);
    std::string body = output.substr(header_end + 4);
    int status_code = 200;
    std::string content_type = "text/html";

    std::istringstream header_stream(headers);
    std::string line;
    while (std::getline(header_stream, line)) {
        if (line.empty() || line == "\r") continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("Status:") == 0) {
            status_code = std::stoi(line.substr(7));
        } else if (line.find("Content-Type:") == 0) {
            content_type = line.substr(13);
            content_type.erase(0, content_type.find_first_not_of(" \t"));
        }
    }

    Response res(config);
    return res.buildResponse(body, status_code, content_type);
}
