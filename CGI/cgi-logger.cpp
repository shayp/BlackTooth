#include <stdio.h>
#include <string.h>
#include <iostream>
#include "cgicc/Cgicc.h"
#include "cgicc/HTTPHTMLHeader.h"
#include "cgicc/HTMLClasses.h"

#define DB_FILENAME "log.dat"
#define MAX_DB_SIZE (1024 * 1024)

using namespace cgicc;
using namespace std;

int main(int argc, char **argv)
{
	Cgicc cgi;
	char* log = new char[MAX_DB_SIZE];	

	memset(log, 0, MAX_DB_SIZE);

	cout << HTTPHTMLHeader() << endl;

	if ("load" == cgi.getEnvironment().getPostData())
	{
		FILE* db_file = fopen(DB_FILENAME, "rb");
		fread(log, 1, MAX_DB_SIZE, db_file);
		fclose(db_file);

		cout << log << endl;
	}
	else
	{
		FILE* db_file = fopen(DB_FILENAME, "wb");
		fwrite(cgi.getEnvironment().getPostData().c_str(), 1, MAX_DB_SIZE, db_file);
		cout << cgi.getEnvironment().getPostData() << endl;
		fclose(db_file);	
	}

	delete log;

	return 0;
}
