package lua;

public class LuaFunction {
    private LuaFunction(){}
    private String p;
    private int f;
    public native void close();
    public native Object[] call(Object... args);
}
