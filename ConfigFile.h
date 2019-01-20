#pragma once
#include <vector>
#include <string>

class ConfigFile
{
private:
	std::vector<std::string> tokens;

protected:

	std::string getToken();

public:
	ConfigFile();
	~ConfigFile();

	void parseFile(std::string filename);
};

/*	ConfigFile format:

// comment

int x = 0
string y = "test"


*/