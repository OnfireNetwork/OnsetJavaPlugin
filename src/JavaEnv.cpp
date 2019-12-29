#include "JavaEnv.hpp"

#ifdef _WIN32
#include <windows.h>
typedef UINT(CALLBACK* JVMDLLFunction)(JavaVM**, void**, JavaVMInitArgs*);
#endif

#include <vector>
#include <tuple>
#include <functional>
#include <sstream>

JavaEnv::JavaEnv(std::string classPath) {
	this->env = nullptr;
	this->vm = nullptr;
	#ifdef _WIN32
	char inputJdkDll[] = "%JAVA_HOME%\\jre\\bin\\server\\jvm.dll";
	TCHAR outputJdkDll[32000];
	DWORD jdkResult = ExpandEnvironmentStrings((LPCTSTR)inputJdkDll, outputJdkDll, sizeof(outputJdkDll) / sizeof(*outputJdkDll));
	char inputJreDll[] = "%JAVA_HOME%\\bin\\server\\jvm.dll";
	TCHAR outputJreDll[32000];
	DWORD jreResult = ExpandEnvironmentStrings((LPCTSTR)inputJreDll, outputJreDll, sizeof(outputJreDll) / sizeof(*outputJreDll));
	if (!jdkResult && !jreResult) {
		Onset::Plugin::Get()->Log("Failed to find JDK/JRE jvm.dll, please ensure Java 8 is installed.");
		return;
	}
	HINSTANCE jvmDLL = LoadLibrary(outputJdkDll);
	if (!jvmDLL) {
		jvmDLL = LoadLibrary(outputJreDll);

		if (!jvmDLL) {
			Onset::Plugin::Get()->Log("Failed to find JDK/JRE jvm.dll, please ensure Java 8 is installed.");
			return;
		}
	}
	JVMDLLFunction createJavaVMFunction = (JVMDLLFunction)GetProcAddress(jvmDLL, "JNI_CreateJavaVM");
	if (!createJavaVMFunction) {
		Onset::Plugin::Get()->Log("Failed to find JDK/JRE jvm.dll, please ensure Java 8 is installed.");
		return;
	}
	#endif

	static std::stringstream optionString;
	optionString << "-Djava.class.path=" << classPath;

	JavaVMInitArgs vm_args;
	auto* options = new JavaVMOption[1];
	char cpoptionString[200] = "";
	std::strcpy(cpoptionString, optionString.str().c_str());

	options[0].optionString = cpoptionString;
	vm_args.version = JNI_VERSION_1_8;
	vm_args.nOptions = 1;
	vm_args.options = options;
	vm_args.ignoreUnrecognized = false;

	#ifdef __linux__ 
		JNI_CreateJavaVM(&this->vm, (void**)&this->env, &vm_args);
	#elif _WIN32
		createJavaVMFunction(&this->vm, (void**)&this->env, &vm_args);
	#endif
}

jobject JavaEnv::ToJavaObject(Lua::LuaValue value)
{
	JNIEnv* jenv = this->GetEnv();
	switch (value.GetType())
	{
	case Lua::LuaValue::Type::STRING:
	{
		return (jobject)jenv->NewStringUTF(value.GetValue<std::string>().c_str());
	} break;
	case Lua::LuaValue::Type::INTEGER:
	{
		jclass jcls = jenv->FindClass("java/lang/Integer");
		return jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "(I)V"), value.GetValue<int>());
	} break;
	case Lua::LuaValue::Type::BOOLEAN:
	{
		jclass jcls = jenv->FindClass("java/lang/Boolean");
		return jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "(Z)V"), value.GetValue<bool>());
	} break;
	case Lua::LuaValue::Type::TABLE:
	{
		jclass jcls = jenv->FindClass("java/util/HashMap");
		jobject jmap = jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "()V"));
		jmethodID putMethod = jenv->GetMethodID(jcls, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

		Lua::LuaTable_t table = value.GetValue<Lua::LuaTable_t>();
		table->ForEach([jenv, this, jmap, putMethod](Lua::LuaValue k, Lua::LuaValue v) {
			jenv->CallObjectMethod(jmap, putMethod, this->ToJavaObject(k), this->ToJavaObject(v));
			});

		return jmap;
	} break;
	case Lua::LuaValue::Type::NIL:
	case Lua::LuaValue::Type::INVALID:
		break;
	default:
		break;
	}

	return NULL;
}

Lua::LuaValue JavaEnv::ToLuaValue(jobject object)
{
	JNIEnv* jenv = this->GetEnv();
	jclass jcls = jenv->GetObjectClass(object);

	if (jenv->IsInstanceOf(object, jenv->FindClass("java/lang/String"))) {
		jstring element = (jstring)object;
		const char* pchars = jenv->GetStringUTFChars(element, nullptr);

		Lua::LuaValue value(pchars);

		jenv->ReleaseStringUTFChars(element, pchars);
		jenv->DeleteLocalRef(element);

		return value;
	}
	else if (jenv->IsInstanceOf(object, jenv->FindClass("java/lang/Integer"))) {
		jmethodID intValueMethod = jenv->GetMethodID(jcls, "intValue", "()I");
		jint result = jenv->CallIntMethod(object, intValueMethod);

		Lua::LuaValue value(result);
		return value;
	}
	else if (jenv->IsInstanceOf(object, jenv->FindClass("java/lang/Boolean"))) {
		jmethodID boolValueMethod = jenv->GetMethodID(jcls, "booleanValue", "()Z");
		jboolean result = jenv->CallBooleanMethod(object, boolValueMethod);

		Lua::LuaValue value(result);
		return value;
	}
	else if (jenv->IsInstanceOf(object, jenv->FindClass("java/util/List"))) {
		jmethodID sizeMethod = jenv->GetMethodID(jcls, "size", "()I");
		jmethodID getMethod = jenv->GetMethodID(jcls, "get", "(I)Ljava/lang/Object;");
		jint len = jenv->CallIntMethod(object, sizeMethod);

		Lua::LuaTable_t table(new Lua::LuaTable);
		for (jint i = 0; i < len; i++) {
			jobject arrayElement = jenv->CallObjectMethod(object, getMethod, i);
			table->Add(i + 1, this->ToLuaValue(arrayElement));
		}

		Lua::LuaValue value(table);
		return value;
	}
	else if (jenv->IsInstanceOf(object, jenv->FindClass("java/util/Map"))) {
		jmethodID getMethod = jenv->GetMethodID(jcls, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");

		jmethodID keySetMethod = jenv->GetMethodID(jcls, "keySet", "()Ljava/util/Set;");
		jobject keySet = jenv->CallObjectMethod(object, keySetMethod);

		jmethodID keySetToArrayMethod = jenv->GetMethodID(jenv->GetObjectClass(keySet), "toArray", "()[Ljava/lang/Object;");
		jobjectArray keyArray = (jobjectArray)jenv->CallObjectMethod(keySet, keySetToArrayMethod);
		int arraySize = jenv->GetArrayLength(keyArray);

		Lua::LuaTable_t table(new Lua::LuaTable);
		for (int i = 0; i < arraySize; i++)
		{
			jobject key = jenv->GetObjectArrayElement(keyArray, i);
			jobject value = jenv->CallObjectMethod(object, getMethod, key);

			table->Add(this->ToLuaValue(key), this->ToLuaValue(value));
		}

		Lua::LuaValue value(table);
		return value;
	}

	return NULL;
}

jobject JavaEnv::CallStatic(std::string className, std::string methodName, std::string signature, jobject* params) {
	size_t paramsLength = sizeof(params);
	if (paramsLength > 0) {
		paramsLength = paramsLength / sizeof(params[0]);
	}
	size_t spos = signature.find(")");
	std::string returnSignature = signature.substr(spos + 1, signature.length() - spos);
	jclass clazz = this->env->FindClass(className.c_str());
	if (clazz == nullptr) return 0;
	jmethodID methodID = this->env->GetStaticMethodID(clazz, methodName.c_str(), signature.c_str());
	if (methodID == nullptr) return 0;
	jobject returnValue = NULL;
	if (!returnSignature.compare("V")) {
		switch (paramsLength) {
		case 0:
			this->env->CallStaticVoidMethod(clazz, methodID);
			break;
		case 1:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0]);
			break;
		case 2:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1]);
			break;
		case 3:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2]);
			break;
		case 4:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3]);
			break;
		case 5:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4]);
			break;
		case 6:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5]);
			break;
		case 7:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6]);
			break;
		case 8:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7]);
			break;
		case 9:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8]);
			break;
		case 10:
			this->env->CallStaticVoidMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9]);
			break;
		default:
			Onset::Plugin::Get()->Log("Too many parameters for CallJavaStaticMethod, 10 max.");
			break;
		}
	}
	else {
		switch (paramsLength) {
		case 0:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID);
			break;
		case 1:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0]);
			break;
		case 2:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1]);
			break;
		case 3:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2]);
			break;
		case 4:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3]);
			break;
		case 5:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4]);
			break;
		case 6:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5]);
			break;
		case 7:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6]);
			break;
		case 8:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7]);
			break;
		case 9:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8]);
			break;
		case 10:
			returnValue = this->env->CallStaticObjectMethod(clazz, methodID, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9]);
			break;
		default:
			Onset::Plugin::Get()->Log("Too many parameters for CallJavaStaticMethod, 10 max.");
			break;
		}
	}
	return returnValue;
}