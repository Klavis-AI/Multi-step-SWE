# Provenance of the files in this workspace

| path | origin |
| --- | --- |
| `SPEC/redis.conf` | `redis.conf` from `github.com/redis/redis` at tag `8.0.6`, unmodified. |
| `SPEC/commands.json` | `commands.json` from `github.com/redis/redis-doc` as of 2025-05-15 (commit `3541d0e20c`) — the command table as it stood when this Redis release shipped. |
| `SPEC/commands/` | `src/commands/*.json` from `github.com/redis/redis` at tag `8.0.6`, unmodified. |
| `SPEC/commands-modules.json` | the command metadata shipped by `redis/redis` (`modules/vector-sets/commands.json`) and by the `RedisJSON`, `RedisBloom` and `RedisTimeSeries` repositories. |
| `SPEC/redis-doc/acl.md` | `docs/management/security/acl.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/client-side-caching.md` | `docs/manual/client-side-caching.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/cluster-spec.md` | `docs/reference/cluster-spec.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/eval-intro.md` | `docs/interact/programmability/eval-intro.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/functions-intro.md` | `docs/interact/programmability/functions-intro.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/keyspace.md` | `docs/manual/keyspace.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/lru-cache.md` | `docs/reference/eviction/index.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/lua-api.md` | `docs/interact/programmability/lua-api.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/notifications.md` | `docs/manual/keyspace-notifications.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/persistence.md` | `docs/management/persistence.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/protocol.md` | `docs/reference/protocol-spec.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/pubsub.md` | `docs/interact/pubsub.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/replication.md` | `docs/management/replication.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/resp3-spec.md` | `github.com/antirez/RESP3/spec.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/streams-intro.md` | `docs/data-types/streams.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `SPEC/redis-doc/transactions.md` | `docs/interact/transactions.md` from `github.com/redis/redis-doc` as of 2025-05-15. |
| `tests/`, `runtest` | Redis's own test suite from `github.com/redis/redis` at tag `8.0.6`. A fixed subset of the suite's files is not included in this copy. |
| `SPEC/BUILD_AND_RUN.md` | written for this task: it describes only how the artifact is built, started and driven, never what it must do. |

Everything under `SPEC/` is reference material. The behaviour it describes is
Redis's, not this task's.
