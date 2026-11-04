package redis;

/**
 * A value created by one of the data types redis 8.0 bundles as modules
 * (ReJSON-RL, MBbloom--, CMSk-type, TopK-TYPE, TDIS-TYPE, TSDB-TYPE, vectorset).
 *
 * Real redis writes module specific RDB opcodes that only the owning module can
 * read back; we use our own compact encoding instead (see {@link ModTypes}),
 * which is why every implementation reports both the TYPE name it must show to
 * clients and a stable id for the on disk form.
 */
public interface ModVal {
    /** The name TYPE / OBJECT ENCODING report for this value. */
    String moduleTypeName();

    /** Stable id of the serializer in {@link ModTypes}. Never renumber. */
    int moduleId();

    /** Self contained payload; {@link ModTypes#load} must accept it forever. */
    byte[] moduleSave();

    /**
     * RedisModuleTypeMethods.copy: only ReJSON-RL and TSDB-TYPE register one,
     * so COPY refuses every other module key with "not supported for this
     * module key".
     */
    default boolean moduleCopyable() { return false; }
}
