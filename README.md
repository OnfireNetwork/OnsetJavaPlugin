# OnsetJavaPlugin
Authors: JanHolger, Digital

### Features
* Create JVMs.
* Communicate between Lua <-> Java.

### Download
Check out Releases for a download to Windows & Linux builds.

### Installation
1. Download OnsetJavaPlugin.dll (Windows) & OnsetJavaPlugin.so (Linux) from Releases ([HERE](https://github.com/OnfireNetwork/OnsetJavaPlugin/releases)) and place inside plugins folder.
1. Ensure Java 8 JDK/JRE 64bit is installed.
1. Enable "OnsetJavaPlugin" as a plugin inside server_config.json.

### Data Types we support
#### Method Parameters
* Lua String -> String (java.lang.String)
* Lua Int -> Integer (java.lang.Integer)
* Lua Number -> Double (java.lang.Double)
* Lua Bool -> Boolean (java.lang.Boolean)
* Lua Table -> Map (java.util.HashMap)
* Lua Function -> LuaFunction (lua.LuaFunction, `OnsetJavaPluginSupport-1.0.jar` required)

#### Return Values
* String (java.lang.String)
* Integer (java.lang.Integer)
* Double (java.lang.Double)
* Boolean (java.lang.Boolean)
* List (java.util.List)
* Map (java.util.Map)

We will be adding more data types later on.

#### Using the lua function support interface
##### Maven
```xml
<repositories>
    <repository>
        <id>jitpack.io</id>
        <url>https://jitpack.io</url>
    </repository>
</repositories>
<dependencies>
    <dependency>
        <groupId>com.github.OnfireNetwork</groupId>
        <artifactId>OnsetJavaPlugin</artifactId>
        <version>2357546bd5</version>
    </dependency>
</dependencies>
```
##### Usage
Java:
```java
public static void example(LuaFunction fn){
    fn.call("Hello from Java!");
    fn.close(); // Always call this when you don't need the function anymore to free memory
}
```
Lua:
```lua
CallJavaStaticMethod(java, "example/Example", "example", "(Llua/LuaFunction;)V", function(msg)
    print(msg)
end)
```

### Lua Functions
#### CreateJava
Create a new JVM with a jar. Returns JVM ID.
```lua
local jvmID = CreateJava(path)
```
* **path** Classpath for the jvm. This parameter is optional. When not provided it will include the "java" directory aswell as all jar files inside or "." when "java" doesn't exist.

#### DestroyJava
Destroy a JVM.
```lua
DestroyJava(jvmID)
```
* **jvmID** ID to the JVM you have created using CreateJava.

#### CallJavaStaticMethod
Call a java static method. Can return information from the Java method, check out Data Types for the types we currently support.
```lua
local dataFromJava = CallJavaStaticMethod(jvmID, className, methodName, methodSignature, args...)
```
* **jvmID** ID to the JVM you have created using CreateJava. Example: 1
* **className** Class name of the class you want to call a method in, must include package path as well. Example: dev/joseph/Test (dev.joseph.Test).
* **methodName** Static method name you want to call. Example: returnTestInt
* **methodSignature** Signature of the method you want to call. Example: (Ljava/lang/Integer;)I
* **args (Optional)** Pass arguments into the method.

#### LinkJavaAdapter
Links a Java class so the native methods below can be used.
```lua
LinkJavaAdapter(jvmID, className)
```
* **jvmID** ID to the JVM you have created using CreateJava. Example: 1
* **className** Class name of the class you want to call a method in, must include package path as well. Example: dev/joseph/Adapter (dev.joseph.Adapter).

### Java Native Methods
You can use a native adapter to call lua functions.
```java
package example;
public class Adapter {
    public native static void callEvent(String event, Object... args);
    public native static Object[] callGlobalFunction(String packageName, String functionName, Object... args);
}
```
```lua
LinkJavaAdapter(java, "example/Adapter")
```
#### callEvent
Java:
```java
Adapter.callEvent("testCallEvent", "hi", Integer.valueOf(123), Boolean.valueOf(true));
```
Lua:
```lua
AddEvent('testCallEvent', function(s,i,b)
    print(s)
    print(i)
    print(b)
end)
```
#### callGlobalFunction
*Make sure to use this method only on the main thread. Using it outside the mainthread can result in unexpected behavior.*  
Java:
```java
Adapter.callGlobalFunction("AddPlayerChatAll", "Hello World");
```
