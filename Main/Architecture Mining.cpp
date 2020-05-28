#pragma warning(disable : 4996)
#pragma warning(disable : 4146)
#include <iostream>
#include "../SourceLoader/SourceLoader.h"
#include "../DependenciesMining/DependenciesMining.h"

int main(int argc, const char** argv) {
	/*sourceLoader::SourceLoader srcLoader("C:\\Users\\user\\Desktop\\demo");
	srcLoader.LoadSources();
	std::vector<std::string> srcs = srcLoader.GetSources();
	srcLoader.PrintSourceFiles();
	*/

	std::vector<std::string> srcs;
	std::string path = argv[1];
	//srcs.push_back(path + "\\classes_simple.cpp");			// OK
	//srcs.push_back(path + "\\fields.cpp");					// OK 
	srcs.push_back(path + "\\friends.cpp");					// Problem with friend templates
	//srcs.push_back(path + "\\member_classes.cpp");			// den eida polu, fainetai OK
	//srcs.push_back(path + "\\methods_args_vars.cpp");			// OK
	//srcs.push_back(path + "\\methods.cpp");					// OK
	//srcs.push_back(path + "\\objects_used_on_methods.cpp");	// OK
	//srcs.push_back(path + "\\template_methods.cpp");			// OK  - problem me full special mesa se template class
	//srcs.push_back(path + "\\template_types.cpp");			// OK
	//srcs.push_back(path + "\\templates.cpp");					// OK

	std::cout << "\n-------------------------------------------------------------------------------------\n\n";
	int result = DependenciesMining::CreateClangTool(argc, argv, srcs);
	std::cout << "\n-------------------------------------------------------------------------------------\n\n";

	//return result;
}