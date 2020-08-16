#include "JavaEnv.hpp"

#ifdef _WIN32
#include <windows.h>
typedef UINT(CALLBACK* JVMDLLFunction)(JavaVM**, void**, JavaVMInitArgs*);
#endif

#include <vector>
#include <tuple>
#include <functional>
#include <sstream>
#include "Plugin.hpp"

void JLuaFunctionClose(JNIEnv* jenv, jobject instance) {
	JavaEnv* env = Plugin::Get()->FindJavaEnv(jenv);
	if (env == nullptr) {
		return;
	}
	env->LuaFunctionClose(instance);
}

jobjectArray JLuaFunctionCall(JNIEnv* jenv, jobject instance, jobjectArray args) {
	JavaEnv* env = Plugin::Get()->FindJavaEnv(jenv);
	if (env == nullptr) {
		return NULL;
	}
	return env->LuaFunctionCall(instance, args);
}

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
	optionString << "-Djava.class.path=" << classPath << " -Xmx256M -Xms256M";

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
		int res = JNI_CreateJavaVM(&this->vm, (void**)&this->env, &vm_args);
	#elif _WIN32
		int res = createJavaVMFunction(&this->vm, (void**)&this->env, &vm_args);
		printf("CreateJava Return: %d", res);
	#endif

	this->luaFunctionClass = this->env->FindClass("lua/LuaFunction");
	if (this->luaFunctionClass != NULL) {
		JNINativeMethod methods[] = {
			{(char*)"close", (char*)"()V", (void*)JLuaFunctionClose },
			{(char*)"call", (char*)"([Ljava/lang/Object;)[Ljava/lang/Object;", (void*)JLuaFunctionCall }
		};
		this->env->RegisterNatives(this->luaFunctionClass, methods, 2);
	}
}

void JavaEnv::LuaFunctionClose(jobject instance) {
	jfieldID fField = this->env->GetFieldID(this->luaFunctionClass, "f", "I");
	int id = this->env->GetIntField(instance, fField);
	this->luaFunctions[id] = NULL;
	this->env->DeleteLocalRef(instance);
}

jobjectArray JavaEnv::LuaFunctionCall(jobject instance, jobjectArray args) {
	jfieldID fField = this->env->GetFieldID(this->luaFunctionClass, "f", "I");
	jfieldID pField = this->env->GetFieldID(this->luaFunctionClass, "p", "Ljava/lang/String;");
	int id = this->env->GetIntField(instance, fField);
	jstring packageName = (jstring) this->env->GetObjectField(instance, pField);
	const char* packageNameStr = this->env->GetStringUTFChars(packageName, nullptr);
	lua_State* L = Plugin::Get()->GetPackageState(packageNameStr);
	this->env->ReleaseStringUTFChars(packageName, packageNameStr);
	this->env->DeleteLocalRef(packageName);
	this->env->DeleteLocalRef(instance);
	int argsLength = this->env->GetArrayLength(args);
	Lua::PushValueToLua(this->luaFunctions[id], L);
	for (jsize i = 0; i < argsLength; i++) {
		Lua::LuaValue val = this->ToLuaValue(this->env->GetObjectArrayElement(args, i));
		Lua::PushValueToLua(val, L);
	}
	this->env->DeleteLocalRef(args);
	Lua::LuaArgs_t ReturnValues;
	int status = lua_pcall(L, argsLength, LUA_MULTRET, 0);
	if (status == LUA_OK) {
		Lua::ParseArguments(L, ReturnValues);
		size_t returnCount = ReturnValues.size();
		lua_pop(L, (int)returnCount);
		jclass objectCls = this->env->FindClass("Ljava/lang/Object;");
		jobjectArray returns = this->env->NewObjectArray((jsize)returnCount, objectCls, NULL);
		this->env->DeleteLocalRef(objectCls);
		for (jsize i = 0; i < (int) returnCount; i++) {
			jobject obj = this->ToJavaObject(L, ReturnValues[i]);
			this->env->SetObjectArrayElement(returns, i, obj);
			this->env->DeleteLocalRef(obj);
		}
		return returns;
	}
	return NULL;
}

jobject JavaEnv::ToJavaObject(lua_State* L, Lua::LuaValue value)
{
	JNIEnv* jenv = this->GetEnv();
	switch (value.GetType())
	{
	case Lua::LuaValue::Type::STRING:
	{
		jobject obj = (jobject)jenv->NewStringUTF(value.GetValue<std::string>().c_str());
		return obj;
	} break;
	case Lua::LuaValue::Type::INTEGER:
	{
		jclass jcls = jenv->FindClass("java/lang/Integer");
		jobject obj = jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "(I)V"), value.GetValue<int>());
		//jenv->DeleteGlobalRef(jcls);
		return obj;
	} break;
	case Lua::LuaValue::Type::NUMBER:
	{
		jclass jcls = jenv->FindClass("java/lang/Double");
		jobject obj = jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "(D)V"), value.GetValue<double>());
		//jenv->DeleteGlobalRef(jcls);
		return obj;
	} break;
	case Lua::LuaValue::Type::BOOLEAN:
	{
		jclass jcls = jenv->FindClass("java/lang/Boolean");
		jobject obj = jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "(Z)V"), value.GetValue<bool>());
		//jenv->DeleteGlobalRef(jcls);
		return obj;
	} break;
	case Lua::LuaValue::Type::TABLE:
	{
		jclass jcls = jenv->FindClass("java/util/HashMap");
		jobject jmap = jenv->NewObject(jcls, jenv->GetMethodID(jcls, "<init>", "()V"));
		jmethodID putMethod = jenv->GetMethodID(jcls, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

		Lua::LuaTable_t table = value.GetValue<Lua::LuaTable_t>();
		table->ForEach([jenv, this, L, jmap, putMethod](Lua::LuaValue k, Lua::LuaValue v) {
			jobject objk = this->ToJavaObject(L, k);
			jobject objv = this->ToJavaObject(L, v);
			jenv->CallObjectMethod(jmap, putMethod, objk, objv);
			//jenv->DeleteGlobalRef(objk);
			//jenv->DeleteGlobalRef(objv);
			});
		//jenv->DeleteGlobalRef(jcls);
		return jmap;
	} break;
	case Lua::LuaValue::Type::FUNCTION:
	{
		if (this->luaFunctionClass == NULL)
			return NULL;
		std::string packageNameStr = Plugin::Get()->GetStatePackage(L);
		jstring packageName = this->env->NewStringUTF(packageNameStr.c_str());
		jobject javaLuaFunction = jenv->NewObject(this->luaFunctionClass, jenv->GetMethodID(this->luaFunctionClass, "<init>", "()V"));
		int id = 1;
		while (this->luaFunctions.count(id)){
			id++;
		}
		this->luaFunctions[id] = value;
		jfieldID fField = this->env->GetFieldID(this->luaFunctionClass, "f", "I");
		this->env->SetIntField(javaLuaFunction, fField, id);
		jfieldID pField = this->env->GetFieldID(this->luaFunctionClass, "p", "Ljava/lang/String;");
		this->env->SetObjectField(javaLuaFunction, pField, packageName);
		//this->env->DeleteGlobalRef(packageName);
		return javaLuaFunction;
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
	jclass stringCls = jenv->FindClass("java/lang/String");
	jclass integerCls = jenv->FindClass("java/lang/Integer");
	jclass doubleCls = jenv->FindClass("java/lang/Double");
	jclass booleanCls = jenv->FindClass("java/lang/Boolean");
	jclass listCls = jenv->FindClass("java/util/List");
	jclass mapCls = jenv->FindClass("java/util/Map");

	if (jenv->IsInstanceOf(object, stringCls)) {
		jstring element = (jstring)object;
		const char* pchars = jenv->GetStringUTFChars(element, nullptr);

		Lua::LuaValue value(pchars);

		jenv->ReleaseStringUTFChars(element, pchars);

		//jenv->DeleteGlobalRef(object);
		//jenv->DeleteGlobalRef(jcls);
		//jenv->DeleteGlobalRef(stringCls);
		//jenv->DeleteGlobalRef(integerCls);
		//jenv->DeleteGlobalRef(doubleCls);
		//jenv->DeleteGlobalRef(booleanCls);
		//jenv->DeleteGlobalRef(listCls);
		//jenv->DeleteGlobalRef(mapCls);

		return value;
	}
	else if (jenv->IsInstanceOf(object, integerCls)) {
		jmethodID intValueMethod = jenv->GetMethodID(jcls, "intValue", "()I");
		jint result = jenv->CallIntMethod(object, intValueMethod);
		//jenv->DeleteGlobalRef(object);
		//jenv->DeleteGlobalRef(jcls);
		//jenv->DeleteGlobalRef(stringCls);
		//jenv->DeleteGlobalRef(integerCls);
		//jenv->DeleteGlobalRef(doubleCls);
		//jenv->DeleteGlobalRef(booleanCls);
		//jenv->DeleteGlobalRef(listCls);
		//jenv->DeleteGlobalRef(mapCls);

		Lua::LuaValue value(result);
		return value;
	}
	else if (jenv->IsInstanceOf(object, doubleCls)) {
		jmethodID doubleValueMethod = jenv->GetMethodID(jcls, "doubleValue", "()D");
		jdouble result = jenv->CallDoubleMethod(object, doubleValueMethod);
		//jenv->DeleteGlobalRef(object);
		//jenv->DeleteGlobalRef(jcls);
		//jenv->DeleteGlobalRef(stringCls);
		//jenv->DeleteGlobalRef(integerCls);
		//jenv->DeleteGlobalRef(doubleCls);
		//jenv->DeleteGlobalRef(booleanCls);
		//jenv->DeleteGlobalRef(listCls);
		//jenv->DeleteGlobalRef(mapCls);

		Lua::LuaValue value(result);
		return value;
	}
	else if (jenv->IsInstanceOf(object, booleanCls)) {
		jmethodID boolValueMethod = jenv->GetMethodID(jcls, "booleanValue", "()Z");
		jboolean result = jenv->CallBooleanMethod(object, boolValueMethod);
		//jenv->DeleteGlobalRef(object);
		//jenv->DeleteGlobalRef(jcls);
		//jenv->DeleteGlobalRef(stringCls);
		//jenv->DeleteGlobalRef(integerCls);
		//jenv->DeleteGlobalRef(doubleCls);
		//jenv->DeleteGlobalRef(booleanCls);
		//jenv->DeleteGlobalRef(listCls);
		//jenv->DeleteGlobalRef(mapCls);

		Lua::LuaValue value((bool) result);
		return value;
	}
	else if (jenv->IsInstanceOf(object, listCls)) {
		jmethodID sizeMethod = jenv->GetMethodID(jcls, "size", "()I");
		jmethodID getMethod = jenv->GetMethodID(jcls, "get", "(I)Ljava/lang/Object;");
		jint len = jenv->CallIntMethod(object, sizeMethod);

		Lua::LuaTable_t table = Lua::LuaTable::Create();
		for (jint i = 0; i < len; i++) {
			jobject arrayElement = jenv->CallObjectMethod(object, getMethod, i);
			table->Add(i + 1, this->ToLuaValue(arrayElement));
		}
		//jenv->DeleteGlobalRef(object);
		//jenv->DeleteGlobalRef(jcls);
		//jenv->DeleteGlobalRef(stringCls);
		//jenv->DeleteGlobalRef(integerCls);
		//jenv->DeleteGlobalRef(doubleCls);
		//jenv->DeleteGlobalRef(booleanCls);
		//jenv->DeleteGlobalRef(listCls);
		//jenv->DeleteGlobalRef(mapCls);
		Lua::LuaValue value(table);
		return value;
	}
	else if (jenv->IsInstanceOf(object, mapCls)) {
		jmethodID getMethod = jenv->GetMethodID(jcls, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");

		jmethodID keySetMethod = jenv->GetMethodID(jcls, "keySet", "()Ljava/util/Set;");
		jobject keySet = jenv->CallObjectMethod(object, keySetMethod);

		jmethodID keySetToArrayMethod = jenv->GetMethodID(jenv->GetObjectClass(keySet), "toArray", "()[Ljava/lang/Object;");
		jobjectArray keyArray = (jobjectArray)jenv->CallObjectMethod(keySet, keySetToArrayMethod);
		//jenv->DeleteLocalRef(keySet);
		int arraySize = jenv->GetArrayLength(keyArray);

		Lua::LuaTable_t table = Lua::LuaTable::Create();
		for (int i = 0; i < arraySize; i++)
		{
			jobject key = jenv->GetObjectArrayElement(keyArray, i);
			jobject value = jenv->CallObjectMethod(object, getMethod, key);

			table->Add(this->ToLuaValue(key), this->ToLuaValue(value));
		}
		//jenv->DeleteGlobalRef(keyArray);
		//jenv->DeleteGlobalRef(object);
		//jenv->DeleteGlobalRef(jcls);
		//jenv->DeleteGlobalRef(stringCls);
		//jenv->DeleteGlobalRef(integerCls);
		//jenv->DeleteGlobalRef(doubleCls);
		//jenv->DeleteGlobalRef(booleanCls);
		//jenv->DeleteGlobalRef(listCls);
		//jenv->DeleteGlobalRef(mapCls);
		Lua::LuaValue value(table);
		return value;
	}
	//jenv->DeleteGlobalRef(object);
	return NULL;
}

jobject JavaEnv::CallStatic(std::string className, std::string methodName, std::string signature, jobject* params, size_t paramsLength) {
	size_t spos = signature.find(")");
	std::string returnSignature = signature.substr(spos + 1, signature.length() - spos);
	jclass clazz = this->env->FindClass(className.c_str());
	if (clazz == nullptr) return NULL;
	jmethodID methodID = this->env->GetStaticMethodID(clazz, methodName.c_str(), signature.c_str());
	if (methodID == nullptr) return NULL;
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
	//this->env->DeleteGlobalRef(clazz);
	for (size_t i = 0; i < paramsLength; i++) {
		//this->env->DeleteGlobalRef(params[i]);
	}
	return returnValue;
}