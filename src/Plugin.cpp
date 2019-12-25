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

JavaVM* Plugin::GetJavaVM(int id)
{
	return this->jvms[id - 1];
}

JNIEnv* Plugin::GetJavaEnv(int id)
{
	return this->jenvs[id - 1];
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

	LUA_DEFINE(CallJavaStaticMethod)
	{
		int id;
		Lua::ParseArguments(L, id);
		if (!Plugin::Get()->GetJavaEnv(id)) return 0;
		JNIEnv* jenv = Plugin::Get()->GetJavaEnv(id);

		Lua::LuaArgs_t arg_list;
		Lua::ParseArguments(L, arg_list);

		int arg_size = static_cast<int>(arg_list.size());
		if (arg_size < 3) return 0;

		std::string className = arg_list[1].GetValue<std::string>();
		std::string methodName = arg_list[2].GetValue<std::string>();
		std::string signature = arg_list[3].GetValue<std::string>();
		size_t spos = signature.find(")");
		std::string returnSignature = signature.substr(spos + 1, signature.length() - spos);

		jobject* params = new jobject[arg_size - 4];
		for (int i = 4; i < arg_size; i++) {
			auto const& value = arg_list[i];

			switch (value.GetType())
			{
				case Lua::LuaValue::Type::STRING:
					{
						params[i - 4] = (jobject)jenv->NewStringUTF(value.GetValue<std::string>().c_str());
					} break;
				case Lua::LuaValue::Type::INTEGER:
					{
						jclass jcls = jenv->FindClass("java/lang/Integer");
						jobject jobj = jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "(I)V"), value.GetValue<int>());
						params[i - 4] = jobj;
					} break;
				case Lua::LuaValue::Type::NIL:
				case Lua::LuaValue::Type::INVALID:
					break;
				default:
					char buffer[50];
					sprintf(buffer, "Unsupported parameter #%d in CallJavaStaticMethod.", i);
					Onset::Plugin::Get()->Log(buffer);
					break;
			}
		}
		jclass clazz = jenv->FindClass(className.c_str());
		if (clazz == nullptr) return 0;

		jmethodID methodID = jenv->GetStaticMethodID(clazz, methodName.c_str(), signature.c_str());
		if (methodID == nullptr) return 0;
		jobject returnValue = nullptr;
		switch (arg_size - 4) {
			case 0:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID);
				break;
			case 1:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0]);
				break;
			case 2:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1]);
				break;
			case 3:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2]);
				break;
			case 4:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3]);
				break;
			case 5:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4]);
				break;
			case 6:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5]);
				break;
			case 7:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6]);
				break;
			case 8:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7]);
				break;
			case 9:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8]);
				break;
			case 10:
				returnValue = jenv->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9]);
				break;
		}
		if (returnValue != nullptr) {
			if (returnSignature.compare("I")) {
				Lua::ReturnValues(L, returnValue);
			}
			if (returnSignature.compare("Ljava.lang.String;")) {
				const char* cstr = jenv->GetStringUTFChars((jstring)returnValue, NULL);
				std::string str = std::string(cstr);
				jenv->ReleaseStringUTFChars((jstring)returnValue, cstr);
				Lua::ReturnValues(L, str);
			}
		}
		return 1;
	});
}
