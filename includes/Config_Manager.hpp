#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>

// Forward declarations
class ConfigManager;
class ConfigParser;
class ConfigTokenizer;

// HTTP methods
enum HttpMethod {
    GET,
    POST,
    DELETE,
    HEAD,
    UNKNOWN
};

// Token definitions
enum class TokenType { Identifier, Symbol, String, Comment, EndOfFile };

struct Token {
    TokenType type;
    std::string value;
};

// AST nodes
struct Directive {
    std::string name;
    std::vector<std::string> args;
};

struct LocationBlock {
  std::string path;
  bool is_regex = false;  // Add this field
  std::vector<Directive> directives;
};

struct ServerBlock {
    std::vector<Directive> directives;
    std::vector<LocationBlock> locations;
};

// Runtime configuration structures
struct RouteConfigFromConfigFile {
    std::string path;
    bool is_regex = false;     // Add this field to indicate if path is a regex
    std::string regex_pattern; // Add this to store the actual regex pattern
    std::string root;
    std::string default_file;
    bool autoindex = false;
    std::vector<std::string> allowed_methods;
    std::map<std::string, std::string> cgi_handlers;
    std::string upload_dir;
    std::size_t client_max_body_size = 1024 * 1024; // Default to 1MB
    std::string redirect;
};

struct ServerConfig {
    int port;
    int sock_fd;
    std::vector<std::string> server_names;
    std::string error_page_404;
    size_t client_max_body_size = 1024 * 1024;
    std::vector<RouteConfigFromConfigFile> routes;
};

// Main configuration manager class
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    // Load and parse configuration
    bool loadFromFile(const std::string& filename);
    bool hasError() const { return m_hasError; }
    const std::string& getErrorMessage() const { return m_errorMessage; }
    
    // Access parsed configuration
    const std::vector<ServerConfig>& getServerConfigs() const { return m_serverConfigs; }
    
    // Utility functions
    void printConfigs() const;
    
private:
    bool m_hasError;
    std::string m_errorMessage;
    std::vector<ServerConfig> m_serverConfigs;
    
    bool validateFilename(const std::string& filename);
    bool validateContent(const std::vector<std::string>& tokens);
    std::vector<ServerConfig> buildConfigs(const std::vector<ServerBlock>& blocks);
    ServerConfig buildServerConfig(const ServerBlock& block);
};

// Helper classes
class ConfigTokenizer {
public:
    ConfigTokenizer(const std::string& input);
    std::vector<Token> tokenize();
    
private:
    std::string m_input;
    size_t m_pos;
};

class ConfigParser {
public:
    ConfigParser(const std::vector<std::string>& tokens);
    std::vector<ServerBlock> parse();
    bool hasError() const { return m_hasError; }
    
private:
    const std::vector<std::string>& m_tokens;
    size_t m_pos;
    bool m_hasError;
    
    std::string peek() const;
    std::string advance();
    bool match(const std::string& expected);
    bool end() const;
    
    ServerBlock parseServer();
    LocationBlock parseLocation();
    Directive parseDirective();
};
