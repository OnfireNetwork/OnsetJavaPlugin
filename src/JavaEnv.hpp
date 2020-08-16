#pragma once

#include <cstring>
#include <jni.h>
#include <map>
#include <PluginSDK.h>

class JavaEnv
{
private:
	JavaVM* vm;
	JNIEnv* env;
	std::map<int, Lua::LuaValue> luaFunctions;
	jclass luaFunctionClass;
public:
	JavaEnv(std::string classPath);
	void Destroy() {
		this->vm->DestroyJavaVM();
	}
	JavaVM* GetVM() {
		return this->vm;
	}
	JNIEnv* GetEnv() {
		return this->env;
	}
	Lua::LuaValue ToLuaValue(jobject object);
	jobject ToJavaObject(lua_State* L, Lua::LuaValue value);
	jobjectArray LuaFunctionCall(jobject instance, jobjectArray args);
	void LuaFunctionClose(jobject instance);
	jobject CallStatic(std::string className, std::string methodName, std::string signature, jobject* params, size_t paramsLength);
};
