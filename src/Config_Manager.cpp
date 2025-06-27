#include "../includes/Config_Manager.hpp"

// ConfigManager implementation
ConfigManager::ConfigManager() : m_hasError(false) {}
ConfigManager::~ConfigManager() {}

bool ConfigManager::loadFromFile(const std::string& filename) {
  // Reset state
  m_hasError = false;
  m_errorMessage.clear();
  m_serverConfigs.clear();
  
  // Validate filename
  if (!validateFilename(filename)) {
      m_hasError = true;
      m_errorMessage = "Invalid configuration filename: " + filename;
      return false;
  }
  
  // Read file content
  std::ifstream configFile(filename);
  if (!configFile.is_open()) {
      m_hasError = true;
      m_errorMessage = "Could not open configuration file: " + filename;
      return false;
  }
  
  // Read line by line and filter out commented lines
  std::string line;
  std::string filteredContent;
  while (std::getline(configFile, line)) {
      // Trim leading whitespace
      size_t firstNonSpace = line.find_first_not_of(" \t");
      
      // Skip empty lines or lines that start with # after trimming
      if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#') {
          continue;
      }
      
      // For lines with inline comments, only keep the part before #
      size_t commentPos = line.find('#');
      if (commentPos != std::string::npos) {
          line = line.substr(0, commentPos);
          // If the line is now empty after removing the comment, skip it
          if (line.find_first_not_of(" \t") == std::string::npos) {
              continue;
          }
      }
      
      filteredContent += line + "\n";
  }
  configFile.close();
  
  // Tokenize
  ConfigTokenizer tokenizer(filteredContent);
  auto tokens = tokenizer.tokenize();
  
  // Extract token values
  std::vector<std::string> tokenValues;
  for (const auto& token : tokens) {
      if (token.type != TokenType::EndOfFile) {
          tokenValues.push_back(token.value);
      }
  }
  
  // Check if tokenization produced any tokens
  if (tokenValues.empty()) {
    m_hasError = true;
    m_errorMessage = "Configuration file is empty or contains only comments";
    return false;
  }

  // Validate token content
  if (!validateContent(tokenValues)) {
      m_hasError = true;
      m_errorMessage = "File does not appear to be a valid configuration file. Expected to start with 'server' block.";
      return false;
  }
  
  // Parse
  ConfigParser parser(tokenValues);
  std::vector<ServerBlock> servers = parser.parse();
  
  if (parser.hasError()) {
      m_hasError = true;
      m_errorMessage = "Configuration file parsing failed. Check the error messages above for details.";
      return false;
  }

  if (servers.empty()) {
    m_hasError = true;
    m_errorMessage = "No server blocks found in configuration file";
    return false;
  }

  // Build runtime configuration
  m_serverConfigs = buildConfigs(servers);
  return true;
}

bool ConfigManager::validateFilename(const std::string& filename) {
  size_t dotPos = filename.find_last_of('.');
  if (dotPos != std::string::npos) {
      std::string extension = filename.substr(dotPos);
      return (extension == ".conf" || extension == ".config" || extension == ".cfg");
  }
  return false;
}

bool ConfigManager::validateContent(const std::vector<std::string>& tokens) {
  if (tokens.empty()) return false;
  
  // Check if first non-comment token is "server"
  for (const auto& token : tokens) {
      if (token == "server") return true;
      if (!token.empty() && token[0] != '#') return false;
  }
  
  return false;
}

std::vector<ServerConfig> ConfigManager::buildConfigs(const std::vector<ServerBlock>& blocks) {
  std::vector<ServerConfig> configs;
  for (const auto& block : blocks) {
      configs.push_back(buildServerConfig(block));
  }
  return configs;
}

ServerConfig ConfigManager::buildServerConfig(const ServerBlock& block) {
  ServerConfig config;

  for (const Directive& dir : block.directives) {
      if (dir.name == "listen" && !dir.args.empty())
          config.port = std::stoi(dir.args[0]);
      else if (dir.name == "server_name")
          config.server_names = dir.args;
      else if (dir.name == "error_page" && dir.args.size() >= 2 && dir.args[0] == "404")
          config.error_page_404 = dir.args[1];
      else if (dir.name == "client_max_body_size" && !dir.args.empty())
          config.client_max_body_size = std::stoul(dir.args[0]);
  }
  // for (const LocationBlock& loc : block.locations) {
  //   RouteConfigFromConfigFile route = buildRouteConfigFromLocation(loc, config.client_max_body_size);
  //   config.routes.push_back(route);
  // }

  for (const LocationBlock& loc : block.locations) {
      RouteConfigFromConfigFile route;
      route.path = loc.path;
      route.is_regex = loc.is_regex;  // Copy the regex flag

      for (const Directive& dir : loc.directives) {
          if (dir.name == "root" && !dir.args.empty())
              route.root = dir.args[0];
          else if (dir.name == "default_file" && !dir.args.empty())
              route.default_file = dir.args[0];
          else if (dir.name == "autoindex" && !dir.args.empty())
              route.autoindex = (dir.args[0] == "on");
          else if (dir.name == "methods")
              route.allowed_methods = dir.args;
          else if (dir.name == "upload_directory" && !dir.args.empty())
              route.upload_dir = dir.args[0];
          else if (dir.name == "cgi" && dir.args.size() == 2)
              route.cgi_handlers[dir.args[0]] = dir.args[1];
          else if (dir.name == "client_max_body_size" && !dir.args.empty())
              route.client_max_body_size = std::stoul(dir.args[0]);
          else if (dir.name == "redirect") {
              route.redirect = dir.args[0];
          }
      }
      route.client_max_body_size = config.client_max_body_size;
      config.routes.push_back(route);
  }

  return config;
}

void ConfigManager::printConfigs() const {
  for (const auto& config : m_serverConfigs) {
      std::cout << "==== Server Config ====\n";
      std::cout << "Port: " << config.port << "\n";
      std::cout << "Server Names: ";
      for (const auto& name : config.server_names)
          std::cout << name << " ";
      std::cout << "\nClient Max Body Size: " << config.client_max_body_size << "\n";
      std::cout << "Error Page 404: " << config.error_page_404 << "\n";

      for (const auto& route : config.routes) {
          std::cout << "  Location: " << route.path << "\n";
          std::cout << "    Root: " << route.root << "\n";
          std::cout << "    Default File: " << route.default_file << "\n";
          std::cout << "    Autoindex: " << (route.autoindex ? "on" : "off") << "\n";
          std::cout << "    Methods: ";
          for (const auto& m : route.allowed_methods)
              std::cout << m << " ";
          std::cout << "\n    Upload Dir: " << route.upload_dir << "\n";
          for (const auto& [ext, handler] : route.cgi_handlers)
              std::cout << "    CGI: " << ext << " => " << handler << "\n";
      }
  }
}

// ConfigTokenizer implementation
ConfigTokenizer::ConfigTokenizer(const std::string& input) : m_input(input), m_pos(0) {}

std::vector<Token> ConfigTokenizer::tokenize() {
  std::vector<Token> tokens;
  int lineNumber = 1;
  bool lineCommented = false;

  std::regex tokenRegex(
      R"((\s+)|(#.*$)|("([^"\\]|\\.)*")|([{};])|([^\s{};"#]+))", std::regex::ECMAScript);

  std::smatch match;

  while (m_pos < m_input.size()) {
      // Reset lineCommented flag at the start of each new line
      if (m_pos == 0 || m_input[m_pos-1] == '\n') {
        lineCommented = false;
      }

      std::string remaining = m_input.substr(m_pos);
      if (std::regex_search(remaining, match, tokenRegex)) {
          std::string tokenStr = match.str();

          for (char c : tokenStr) {
            if (c == '\n') lineNumber++;
          }
          
          // Move position forward by the token's position and length
          m_pos += match.position();
          
          if (match[1].matched) {
              // Skip whitespace
          } else if (match[2].matched) {
              lineCommented = true;
              // No need to add comment tokens
          } else if (!lineCommented) {
              // Only add tokens if the line is not commented
              if (match[3].matched) {
                  tokens.push_back({TokenType::String, tokenStr});
              } else if (match[5].matched) {
                  tokens.push_back({TokenType::Symbol, tokenStr});
              } else if (match[6].matched) {
                  tokens.push_back({TokenType::Identifier, tokenStr});
              }
          }
          
          // Always advance past the current token
          m_pos += match.length();
      } else {
        std::cerr << "Error: Unexpected character at line " << lineNumber << std::endl;
        break;
      }
  }

  tokens.push_back({TokenType::EndOfFile, ""});
  return tokens;
}

// ConfigParser implementation
ConfigParser::ConfigParser(const std::vector<std::string>& tokens)
  : m_tokens(tokens), m_pos(0), m_hasError(false) {}

std::vector<ServerBlock> ConfigParser::parse() {
  std::vector<ServerBlock> servers;
  while (!end()) {
    if (peek() == "server") {
        servers.push_back(parseServer());
    } else {
      std::string token = peek();
      std::cerr << "Error: Unexpected token '" << token << "'. Expected 'server'" << std::endl;
      advance();
      m_hasError = true;
    }
  }
  return servers;
}

std::string ConfigParser::peek() const {
  size_t pos = m_pos;
  while (pos < m_tokens.size()) {
    // Skip comments - assuming these are stored as tokens starting with #
    if (!m_tokens[pos].empty() && m_tokens[pos][0] == '#') {
        pos++;
        continue;
    }
    return m_tokens[pos];
  }
  return "";
}

std::string ConfigParser::advance() {
  while (m_pos < m_tokens.size()) {
    std::string token = m_tokens[m_pos++];
    // Skip comments
    if (!token.empty() && token[0] == '#') {
        continue;
    }
    return token;
  }
  return "";
}

bool ConfigParser::match(const std::string& expected) {
  if (peek() == expected) {
      advance();
      return true;
  }
  return false;
}

bool ConfigParser::end() const {
  return m_pos >= m_tokens.size();
}

ServerBlock ConfigParser::parseServer() {
  ServerBlock server;
  match("server");
  if (!match("{")) {
    std::cerr << "Error: Expected '{' after 'server', but found '" << peek() << "'" << std::endl;
      m_hasError = true;
      return server;
  }
  
  while (!match("}")) {
      if (end()) {
          std::cerr << "Error: Unexpected end of file, expected '}' to close server block" << std::endl;
          m_hasError = true;
          break;
      }
      
      if (peek() == "location") {
          server.locations.push_back(parseLocation());
      } else {
          server.directives.push_back(parseDirective());
      }
  }
  
  return server;
}

LocationBlock ConfigParser::parseLocation() {
  LocationBlock location;
  match("location");
  
  if (end()) {
      std::cerr << "Error: Unexpected end of file, expected location path" << std::endl;
      m_hasError = true;
      return location;
  }
  
  // Check if next token is a tilde (indicating regex)
  if (peek() == "~") {
    location.is_regex = true;
    advance(); // Consume the tilde
    
    if (end()) {
        std::cerr << "Error: Unexpected end of file after '~', expected regex pattern" << std::endl;
        m_hasError = true;
        return location;
    }
    
    // Get the regex pattern
    location.path = advance();
  } else {
    // Regular path
    location.path = advance();;
  }

  if (!match("{")) {
    std::cerr << "Error: Expected '{' after location " 
              << (location.is_regex ? "regex pattern" : "path") << " '" 
              << location.path << "', but found '" << peek() << "'" << std::endl;
    m_hasError = true;
    return location;
  }
  
  while (!match("}")) {
      if (end()) {
        std::cerr << "Error: Unexpected end of file, expected '}' to close location block for path '" 
                  << location.path << "'" << std::endl;
          m_hasError = true;
          break;
      }
      
      location.directives.push_back(parseDirective());
  }
  
  return location;
}

Directive ConfigParser::parseDirective() {
  Directive directive;
  
  if (end()) {
      std::cerr << "Error: Unexpected end of file, expected directive name" << std::endl;
      m_hasError = true;
      return directive;
  }
  
  directive.name = advance();
  
  while (peek() != ";" && !end()) {
      directive.args.push_back(advance());
  }
  
  if (!match(";")) {
    std::cerr << "Error: Expected ';' after directive '" << directive.name 
              << "' with " << directive.args.size() << " arguments, but found '" 
              << (end() ? "end of file" : peek()) << "'" << std::endl;
      m_hasError = true;
  }
  
  return directive;
}
