#include "../includes/Utils.hpp"
#include <fstream>
#include <sstream>

std::string read_file (const std::string& path) {
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return "";
	std::ostringstream ss;
	ss << file.rdbuf();
	return ss.str();
}