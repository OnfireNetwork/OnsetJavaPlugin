# OnsetJavaPlugin
Authors: JanHolger, Digital

### Features
* Create JVMs.
* Communicate from Lua <-> Java.

### Download
Check out Releases for a download to Windows & Linux builds.

### Issues
If you have the following error in console while loading our Java plugin,
```
[2019-12-27 20:01:37] [info] Loading plugin "OnsetJavaPlugin"
[2019-12-27 20:01:37] [error] Failed loading "OnsetJavaPlugin": The specified module could not be found.
```
This is typically an issue with Windows users. To fix this, add your Java 8 bin/server folder to your PATH configuration. Example of JDK (or JRE) path:
```C:\Program Files\Java\jdk1.8.0_221\jre\bin\server```
Make sure your Java installation is 64 bit otherwise it won't have the required JNI libraries we use.

### Data Types we support
#### Method Parameters
* Lua String -> String (java.lang.String)
* Lua Int -> Integer (java.lang.Integer)
* Lua Bool -> Boolean (java.lang.Boolean)
* Lua Table -> Map (java.util.HashMap)

#### Return Values
* String (java.lang.String)
* Integer (java.lang.Integer)
* Boolean (java.lang.Boolean)
* List (java.util.List)
* Map (java.util.Map)

We will be adding more data types later on.

### Lua Functions
#### CreateJava
Create a new JVM with a jar. Returns JVM ID.
```lua
local jvmID = CreateJava(path)
```
* **path** Relative path to the jar file, relative from the root server directory. Example: path/to/file.jar

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
#### callEvent
Call Lua events from Java.
```java
public class Adapter {
    public native static void callEvent(String event, List<Object> args);
}
```
Example:
```java
Adapter.callEvent("testCallEvent", Arrays.asList("lol", "haha", "yeah"));
Adapter.callEvent("testCallEvent", Arrays.asList(1, 2, "hi", 384, "yeeep", true, false));
```
```lua
function testCallEvent(arg1)
  print ('first arg is ' .. arg1)
end
AddEvent('testCallEvent', testCallEvent)
```
