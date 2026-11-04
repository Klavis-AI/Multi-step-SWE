# Provenance of the files in this workspace

| path | origin |
| --- | --- |
| `SPEC/redis.conf` | `redis.conf` from `github.com/redis/redis` at tag `7.0.15`, unmodified. |
| `SPEC/commands.json` | `commands.json` from `github.com/redis/redis-doc` as of 2022-05-15 (commit `c7b188aaa6`) — the command table as it stood when this Redis release shipped. |
| `SPEC/commands/` | `src/commands/*.json` from `github.com/redis/redis` at tag `7.0.15`, unmodified. |
| `SPEC/redis-doc/acl.md` | `docs/manual/security/acl.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/client-side-caching.md` | `docs/manual/client-side-caching.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/cluster-spec.md` | `docs/reference/cluster-spec.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/eval-intro.md` | `docs/manual/programmability/eval-intro.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/functions-intro.md` | `docs/manual/programmability/functions-intro.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/keyspace.md` | `docs/manual/data-types/data-types-tutorial.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/lru-cache.md` | `docs/manual/eviction.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/lua-api.md` | `docs/manual/programmability/lua-api.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/notifications.md` | `docs/manual/keyspace-notifications.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/persistence.md` | `docs/manual/persistence.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/protocol.md` | `docs/reference/protocol-spec.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/pubsub.md` | `docs/manual/pubsub.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/replication.md` | `docs/manual/replication.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/resp3-spec.md` | `github.com/antirez/RESP3/spec.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/streams-intro.md` | `docs/manual/data-types/streams.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `SPEC/redis-doc/transactions.md` | `docs/manual/transactions.md` from `github.com/redis/redis-doc` as of 2022-05-15. |
| `tests/`, `runtest` | Redis's own test suite from `github.com/redis/redis` at tag `7.0.15`. A fixed subset of the suite's files is not included in this copy. |
| `SPEC/BUILD_AND_RUN.md` | written for this task: it describes only how the artifact is built, started and driven, never what it must do. |

Everything under `SPEC/` is reference material. The behaviour it describes is
Redis's, not this task's.
