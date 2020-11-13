#include <sstream>
#include <iostream>
#include <cstring>
#include <string>

#include <filesystem>
namespace fs = std::filesystem;

#ifdef _WIN32
#include <windows.h>
#endif

#include "Plugin.hpp"

#ifdef LUA_DEFINE
# undef LUA_DEFINE
#endif
#define LUA_DEFINE(name) Define(#name, [](lua_State *L) -> int

#ifdef _WIN32
typedef UINT(CALLBACK* JVMDLLFunction)(JavaVM**, void**, JavaVMInitArgs*);
#endif

Lua::LuaArgs_t CallLuaFunction(lua_State* ScriptVM, const char* LuaFunctionName, Lua::LuaArgs_t* Arguments) {
	Lua::LuaArgs_t ReturnValues;
	int ArgCount = lua_gettop(ScriptVM);
	lua_getglobal(ScriptVM, LuaFunctionName);
	int argc = 0;
	if (Arguments) {
		for (auto const& e : *Arguments) {
			Lua::PushValueToLua(e, ScriptVM);
			argc++;
		}
	}
	int Status = lua_pcall(ScriptVM, argc, LUA_MULTRET, 0);
	ArgCount = lua_gettop(ScriptVM) - ArgCount;
	if (Status == LUA_OK) {
		Lua::ParseArguments(ScriptVM, ReturnValues);
		lua_pop(ScriptVM, ArgCount);
	}
	return ReturnValues;
}

int Plugin::CreateJava(std::string classPath)
{
	int id = 0;
	while (this->jenvs[id] != nullptr)
		id++;
	this->jenvs[id] = new JavaEnv(classPath);
	return id + 1;
}

void Plugin::DestroyJava(int id)
{
	this->jenvs[id - 1]->Destroy();
	this->jenvs[id - 1] = nullptr;
}

JavaEnv* Plugin::GetJavaEnv(int id)
{
	return this->jenvs[id - 1];
}

JavaEnv* Plugin::FindJavaEnv(JNIEnv* jenv) {
	int i;
	for (i = 0; i < 30; i++) {
		if (this->jenvs[i] != nullptr) {
			if (this->jenvs[i]->GetEnv() == jenv) {
				return this->jenvs[i];
			}
		}
	}
	return nullptr;
}

void CallEvent(JNIEnv* jenv, jclass jcl, jstring event, jobjectArray argsList) {
	JavaEnv* env = Plugin::Get()->FindJavaEnv(jenv);
	if (env == nullptr) {
		return;
	}
	(void)jcl;

	const char* eventStr = jenv->GetStringUTFChars(event, nullptr);
	Lua::LuaArgs_t args;

	int argsCount = jenv->GetArrayLength(argsList);

	for (jsize i = 0; i < argsCount; i++) {
		jobject arrayElement = jenv->GetObjectArrayElement(argsList, i);
		args.push_back(env->ToLuaValue(arrayElement));
	}

	Onset::Plugin::Get()->CallEvent(eventStr, &args);

	jenv->ReleaseStringUTFChars(event, eventStr);
}

jobjectArray CallGlobal(JNIEnv* jenv, jclass jcl, jstring packageName, jstring functionName, jobjectArray args) {
	JavaEnv* env = Plugin::Get()->FindJavaEnv(jenv);
	if (env == nullptr) {
		return NULL;
	}

	(void)jcl;

	const char* packageNameStr = jenv->GetStringUTFChars(packageName, nullptr);
	const char* functionNameStr = jenv->GetStringUTFChars(functionName, nullptr);
	int argsLength = jenv->GetArrayLength(args);
	Lua::LuaArgs_t luaArgs;
	for (jsize i = 0; i < argsLength; i++) {
		luaArgs.push_back(env->ToLuaValue(jenv->GetObjectArrayElement(args, i)));
	}

	auto luaReturns = CallLuaFunction(Plugin::Get()->GetPackageState(packageNameStr), functionNameStr, &luaArgs);
	size_t returnsLength = luaReturns.size();
	jclass objectCls = jenv->FindClass("Ljava/lang/Object;");
	jobjectArray returns = jenv->NewObjectArray((jsize)returnsLength, objectCls, NULL);
	for (jsize i = 0; i < (int)returnsLength; i++) {
		jobject o = env->ToJavaObject(Plugin::Get()->GetPackageState(packageNameStr), luaReturns[i]);
		jenv->SetObjectArrayElement(returns, i, o);
		jenv->DeleteGlobalRef(o);
	}
	jenv->ReleaseStringUTFChars(packageName, packageNameStr);
	jenv->ReleaseStringUTFChars(functionName, functionNameStr);
	return returns;
}

Plugin::Plugin()
{
	for (int i = 0; i < 30; i++)
		this->jenvs[i] = nullptr;

	LUA_DEFINE(CreateJava)
	{
		Lua::LuaArgs_t args;
		Lua::ParseArguments(L, args);
		int id = -1;
		if (args.size() > 0) {
			id = Plugin::Get()->CreateJava(args[0].GetValue<std::string>());
		}
		else {
			std::string classPath = ".";
			if (fs::exists("java")) {
				classPath = "java";
				for (const auto& entry : fs::directory_iterator("java")) {
					if (!entry.is_directory()) {
						std::string name = entry.path().string();
						if (name.length() > 4) {
							if (!name.substr(name.length() - 4, 4).compare(".jar")) {
#ifdef _WIN32
								classPath += ";" + name;
#else
								classPath += ":" + name;
#endif
							}
						}
					}
				}
			}
			id = Plugin::Get()->CreateJava(classPath);
		}
		if (id < 0) return 0;
		return Lua::ReturnValues(L, id);
	});

LUA_DEFINE(DestroyJava)
{
	int id;
	Lua::ParseArguments(L, id);
	Plugin::Get()->DestroyJava(id);
	return 1;
});

LUA_DEFINE(LinkJavaAdapter)
{
	int id;
	Lua::ParseArguments(L, id);
	if (!Plugin::Get()->GetJavaEnv(id)) return 0;
	JNIEnv* jenv = Plugin::Get()->GetJavaEnv(id)->GetEnv();
	Lua::LuaArgs_t arg_list;
	Lua::ParseArguments(L, arg_list);

	int arg_size = static_cast<int>(arg_list.size());
	if (arg_size < 2) return 0;

	std::string className = arg_list[1].GetValue<std::string>();
	jclass clazz = jenv->FindClass(className.c_str());
	if (clazz == nullptr) return 0;
	JNINativeMethod methods[] = {
		{(char*)"callEvent", (char*)"(Ljava/lang/String;[Ljava/lang/Object;)V", (void*)CallEvent },
		{(char*)"callGlobalFunction", (char*)"(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)[Ljava/lang/Object;", (void*)CallGlobal }
	};
	jenv->RegisterNatives(clazz, methods, 2);
	Lua::ReturnValues(L, 1);
	return 1;
});

LUA_DEFINE(CallJavaStaticMethod)
{
	int id;
	Lua::ParseArguments(L, id);

	if (!Plugin::Get()->GetJavaEnv(id)) return 0;
	JavaEnv* env = Plugin::Get()->GetJavaEnv(id);

	Lua::LuaArgs_t arg_list;
	Lua::ParseArguments(L, arg_list);

	int arg_size = static_cast<int>(arg_list.size());
	if (arg_size < 4) return 0;
	lua_pop(L, arg_size);

	std::string className = arg_list[1].GetValue<std::string>();
	std::string methodName = arg_list[2].GetValue<std::string>();
	std::string signature = arg_list[3].GetValue<std::string>();
	jobject* params = new jobject[arg_size - 4];
	for (int i = 4; i < arg_size; i++) {
		auto const& value = arg_list[i];
		params[i - 4] = env->ToJavaObject(L, value);
	}
	jobject returnValue = Plugin::Get()->GetJavaEnv(id)->CallStatic(className, methodName, signature, params, arg_size - 4);
	delete[] params;
	if (returnValue != NULL) {
		Lua::LuaValue value = env->ToLuaValue(returnValue);
		if (!(value == NULL)) {
			switch (value.GetType())
			{
			case Lua::LuaValue::Type::STRING:
				Lua::ReturnValues(L, value.GetValue<std::string>().c_str());
				break;
			case Lua::LuaValue::Type::INTEGER:
				Lua::ReturnValues(L, value.GetValue<int>());
				break;
			case Lua::LuaValue::Type::BOOLEAN:
				Lua::ReturnValues(L, value.GetValue<bool>());
				break;
			case Lua::LuaValue::Type::TABLE:
				Lua::ReturnValues(L, value.GetValue<Lua::LuaTable_t>());
				break;
			default:
				break;
			}
		}
	}
	else {
		Lua::ReturnValues(L, 1);
	}
	return 1;
});
}