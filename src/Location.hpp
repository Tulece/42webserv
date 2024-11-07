#ifndef LOCATION_HPP
#define LOCATION_HPP

#include <string>
#include <map>
#include <vector>


struct Location {
	std::string path;
	std::map<std::string, std::string> options;
	std::vector<std::string> allowedMethods;
	int clientMaxBodySize;
	int returnCode;
	std::string returnUrl;
	std::string uploadPath;

	Location() : clientMaxBodySize(-1), returnCode(0) {}
};

#endif
