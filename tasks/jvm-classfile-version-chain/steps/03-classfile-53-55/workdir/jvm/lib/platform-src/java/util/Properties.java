package java.util;
public class Properties {
    private final HashMap<Object, Object> map = new HashMap<Object, Object>();
    public Properties() {}
    public String getProperty(String key) {
        Object v = map.get(key);
        return (v instanceof String) ? (String) v : null;
    }
    public String getProperty(String key, String def) {
        String v = getProperty(key);
        return v == null ? def : v;
    }
    public synchronized Object setProperty(String key, String value) { return map.put(key, value); }
    public Object get(Object key) { return map.get(key); }
    public synchronized Object put(Object key, Object value) { return map.put(key, value); }
    public Set<Object> keySet() { return map.keySet(); }
    public int size() { return map.size(); }
    public boolean containsKey(Object k) { return map.containsKey(k); }
}
