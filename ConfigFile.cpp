#include "pch.h"
#include "ConfigFile.h"

#include <fstream>

static int searchChar(const unsigned char* str, unsigned char search) {
	const unsigned char* ptr = str;
	while (*ptr++ != '\0') {
		if (*ptr == search)
			return search;
	}
	return 0;
}

static int getNextToken(std::istream& in, std::string& buffer) {
	/*
	static const unsigned char* acceptableChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_";
	static const unsigned char* acceptableCtrlChars = "=";
	buffer.clear();
	while (!in.eof()) {
		unsigned char ch = in.get();
		if (searchChar(acceptableChars, ch)) {
			buffer += ch;
		}
		else if (searchChar(acceptableCtrlChars, ch)) {
			return 1;
		}
		else {
			break;
		}
	}
	return 0;
	*/
}

void ConfigFile::parseFile(std::string filename) {
	std::ifstream stream(filename);
}

ConfigFile::ConfigFile()
{
}


ConfigFile::~ConfigFile()
{
}
