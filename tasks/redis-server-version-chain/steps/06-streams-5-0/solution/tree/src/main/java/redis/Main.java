package redis;

public final class Main {
    public static void main(String[] args) {
        try {
            new Server().start(args);
        } catch (Throwable t) {
            t.printStackTrace();
            System.exit(1);
        }
    }
}
