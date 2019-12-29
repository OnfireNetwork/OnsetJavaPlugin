#include <sstream>
#include <iostream>
#include <cstring>
#include <string>

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

void CallEvent(JNIEnv* jenv, jclass jcl, jstring event, jobject argsList) {
	JavaEnv* env = Plugin::Get()->FindJavaEnv(jenv);
	if (env == nullptr) {
		return;
	}
	
	(void) jcl;

	const char* eventStr = jenv->GetStringUTFChars(event, nullptr);
	auto args = new Lua::LuaArgs_t();

	if (jenv->IsInstanceOf(argsList, jenv->FindClass("java/util/List"))) {
		jclass argsCls = jenv->GetObjectClass(argsList);
		jmethodID sizeMethod = jenv->GetMethodID(argsCls, "size", "()I");
		jmethodID getMethod = jenv->GetMethodID(argsCls, "get", "(I)Ljava/lang/Object;");
		jint len = jenv->CallIntMethod(argsList, sizeMethod);

		for (jint i = 0; i < len; i++) {
			jobject arrayElement = jenv->CallObjectMethod(argsList, getMethod, i);
			args->push_back(env->ToLuaValue(arrayElement));
		}
	}

	Onset::Plugin::Get()->CallEvent(eventStr, args);

	jenv->ReleaseStringUTFChars(event, eventStr);
	jenv->DeleteLocalRef(event);
}

Plugin::Plugin()
{
	for (int i = 0; i < 30; i++)
		this->jenvs[i] = nullptr;

	LUA_DEFINE(CreateJava)
	{
		std::string classPath;
		Lua::ParseArguments(L, classPath);
		int id = Plugin::Get()->CreateJava(classPath);
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
			{(char*)"callEvent", (char*)"(Ljava/lang/String;Ljava/util/List;)V", (void*)CallEvent },
		};
		jenv->RegisterNatives(clazz, methods, 1);
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

		std::string className = arg_list[1].GetValue<std::string>();
		std::string methodName = arg_list[2].GetValue<std::string>();
		std::string signature = arg_list[3].GetValue<std::string>();
		jobject* params = new jobject[arg_size - 4];
		for (int i = 4; i < arg_size; i++) {
			auto const& value = arg_list[i];
			params[i - 4] = env->ToJavaObject(value);
		}

		jobject returnValue = Plugin::Get()->GetJavaEnv(id)->CallStatic(className, methodName, signature, params);
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
