#include <sstream>
#include <iostream>
#include <string>

#include "Plugin.hpp"

#ifdef LUA_DEFINE
# undef LUA_DEFINE
#endif
#define LUA_DEFINE(name) Define(#name, [](lua_State *L) -> int

int Plugin::CreateJava(std::string classPath)
{
	int id = 0;
	while (this->jvms[id] != nullptr){
		id++;
	}
	static std::stringstream optionString;
	optionString << "-Djava.class.path=" << classPath;
	JavaVMInitArgs vm_args;
	auto* options = new JavaVMOption[1];
	char cpoptionString[200] = "";
	strcpy(cpoptionString, optionString.str().c_str());
	options[0].optionString = cpoptionString;
	vm_args.version = JNI_VERSION_1_8;
	vm_args.nOptions = 1;
	vm_args.options = options;
	vm_args.ignoreUnrecognized = false;
	JNI_CreateJavaVM(&this->jvms[id], (void**)&this->jenvs[id], &vm_args);
	return id + 1;
}

void Plugin::DestroyJava(int id)
{
	this->jvms[id - 1]->DestroyJavaVM();
	this->jvms[id - 1] = nullptr;
	this->jenvs[id - 1] = nullptr;
}

Plugin::Plugin()
{
	for (int i = 0; i < 30; i++) {
		this->jvms[i] = nullptr;
	}
	LUA_DEFINE(CreateJava)
	{
		std::string classPath;
		Lua::ParseArguments(L, classPath);
		int id = Plugin::Get()->CreateJava(classPath);
		return Lua::ReturnValues(L, id);
	});
	LUA_DEFINE(DestroyJava)
	{
		int id;
		Lua::ParseArguments(L, id);
		Plugin::Get()->DestroyJava(id);
		return 1;
	});
}
