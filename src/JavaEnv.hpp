#pragma once

#include <cstring>
#include <jni.h>
#include <PluginSDK.h>

class JavaEnv
{
private:
	JavaVM* vm;
	JNIEnv* env;
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
	jobject ToJavaObject(Lua::LuaValue value);
	jobject CallStatic(std::string className, std::string methodName, std::string signature, jobject* params, size_t paramsLength);
};

