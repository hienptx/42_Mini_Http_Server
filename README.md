# Webserv

A simple C++ HTTP web server implementation following the 42 school project specifications.

## 🛠️ Build Instructions

This project requires a C++17-compatible compiler.

### 1. Clone the repository

```bash
git clone https://github.com/hienptx/42_Mini_Http_Server.git
cd 42_Mini_Http_Server
```

### 2. Build the project

```bash
make
```

This compiles all source files in the `src/` directory and creates the `webserv` executable in the project root.

## Usage

```bash
./webserv webserv.conf
```

* `webserv.conf` is the configuration file describing server blocks, ports, routes, etc.
* The format and syntax follow an nginx-style layout.

## Project Structure

```
.
├── includes/        # Header files
├── obj/             # Compiled object files (generated)
├── src/             # Source code files
├── webserv.conf     # Example configuration file
├── webserv          # Final executable (after make)
└── Makefile         # Build system
```

## Features

* HTTP 1.1 compliant
* Supports GET, POST, and DELETE methods
* Static file serving
* CGI execution
* Configurable routing via `webserv.conf`
* Handles multiple client connections using select/poll

## Example

Run the server:

```bash
./webserv webserv.conf
```

Access it in your browser:

```
http://localhost:<configured_port>
```

## Cleaning Up

```bash
make clean   # Remove object files
make fclean  # Remove object files and executable
make re      # Full rebuild
```

## License

This project is for educational purposes under the 42 school curriculum.
