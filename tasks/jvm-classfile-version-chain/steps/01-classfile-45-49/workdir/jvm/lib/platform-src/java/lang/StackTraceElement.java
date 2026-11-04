package java.lang;
public final class StackTraceElement {
    private String declaringClass;
    private String methodName;
    private String fileName;
    private int lineNumber;
    public StackTraceElement(String declaringClass, String methodName, String fileName, int lineNumber) {
        this.declaringClass = declaringClass;
        this.methodName = methodName;
        this.fileName = fileName;
        this.lineNumber = lineNumber;
    }
    public String getClassName() { return declaringClass; }
    public String getMethodName() { return methodName; }
    public String getFileName() { return fileName; }
    public int getLineNumber() { return lineNumber; }
    public String toString() {
        String loc;
        if (lineNumber == -2) loc = "Native Method";
        else if (fileName == null) loc = "Unknown Source";
        else if (lineNumber >= 0) loc = fileName + ":" + lineNumber;
        else loc = fileName;
        return declaringClass + "." + methodName + "(" + loc + ")";
    }
}
