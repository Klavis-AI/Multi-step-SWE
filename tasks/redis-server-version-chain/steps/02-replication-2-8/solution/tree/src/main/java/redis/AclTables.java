package redis;

import java.util.*;

/**
 * Static command metadata used by the ACL subsystem: the ACL categories of every
 * command (as computed by redis 8.0.6 from the explicit ACL_CATEGORY_* bits in
 * src/commands.def plus setImplicitACLCategories()), and the key specs used by
 * getKeysUsingKeySpecs() (src/db.c).
 *
 * The tables below are generated from srctrees/8.0.6/src/commands.def.
 */
public final class AclTables {
    private AclTables() {}

    /** ACLDefaultCommandCategories[] order -- ACL CAT emits exactly this order. */
    public static final String[] CATS = {
        "keyspace", "read", "write", "set", "sortedset", "list", "hash", "string",
        "bitmap", "hyperloglog", "geo", "stream", "pubsub", "admin", "fast", "slow",
        "blocking", "dangerous", "connection", "transaction", "scripting",
    };

    /** "<fullname> <hexmask>[!]"; the trailing '!' marks CMD_NO_AUTH. */
    static final String[] CAT_DATA = {
        "acl 8000 acl|cat 8000 acl|deluser 2a000 acl|dryrun 2a000 acl|genpass 8000 acl|getuser 2a000 acl|help 8000 acl|list 2a000",
        "acl|load 2a000 acl|log 2a000 acl|save 2a000 acl|setuser 2a000 acl|users 2a000 acl|whoami 8000 append 4084 asking 44000",
        "auth 44000! bgrewriteaof 2a000 bgsave 2a000 bitcount 8102 bitfield 8104 bitfield_ro 4102 bitop 8104 bitpos 8102",
        "blmove 18024 blmpop 18024 blpop 18024 brpop 18024 brpoplpush 18024 bzmpop 18014 bzpopmax 14014 bzpopmin 14014",
        "client 8000 client|caching 48000 client|getname 48000 client|getredir 48000 client|help 48000 client|id 48000 client|info 48000 client|kill 6a000",
        "client|list 6a000 client|no-evict 6a000 client|no-touch 48000 client|pause 6a000 client|reply 48000 client|setinfo 48000 client|setname 48000 client|tracking 48000",
        "client|trackinginfo 48000 client|unblock 6a000 client|unpause 6a000 cluster 8000 cluster|addslots 2a000 cluster|addslotsrange 2a000 cluster|bumpepoch 2a000 cluster|count-failure-reports 2a000",
        "cluster|countkeysinslot 8000 cluster|delslots 2a000 cluster|delslotsrange 2a000 cluster|failover 2a000 cluster|flushslots 2a000 cluster|forget 2a000 cluster|getkeysinslot 8000 cluster|help 8000",
        "cluster|info 8000 cluster|keyslot 8000 cluster|links 8000 cluster|meet 2a000 cluster|myid 8000 cluster|myshardid 8000 cluster|nodes 8000 cluster|replicas 2a000",
        "cluster|replicate 2a000 cluster|reset 2a000 cluster|saveconfig 2a000 cluster|set-config-epoch 2a000 cluster|setslot 2a000 cluster|shards 8000 cluster|slaves 2a000 cluster|slots 8000",
        "command 48000 command|count 48000 command|docs 48000 command|getkeys 48000 command|getkeysandflags 48000 command|help 48000 command|info 48000 command|list 48000",
        "config 8000 config|get 2a000 config|help 8000 config|resetstat 2a000 config|rewrite 2a000 config|set 2a000 copy 8005 dbsize 4003",
        "debug 2a000 decr 4084 decrby 4084 del 8005 discard 84000 dump 8003 echo 44000 eval 108000",
        "eval_ro 108000 evalsha 108000 evalsha_ro 108000 exec 88000 exists 4003 expire 4005 expireat 4005 expiretime 4003",
        "failover 2a000 fcall 108000 fcall_ro 108000 flushall 28005 flushdb 28005 function 8000 function|delete 108004 function|dump 108000",
        "function|flush 108004 function|help 108000 function|kill 108000 function|list 108000 function|load 108004 function|restore 108004 function|stats 108000 geoadd 8404",
        "geodist 8402 geohash 8402 geopos 8402 georadius 8404 georadius_ro 8402 georadiusbymember 8404 georadiusbymember_ro 8402 geosearch 8402",
        "geosearchstore 8404 get 4082 getbit 4102 getdel 4084 getex 4084 getrange 8082 getset 4084 hdel 4044",
        "hello 44000! hexists 4042 hexpire 4044 hexpireat 4044 hexpiretime 4042 hget 4042 hgetall 8042 hgetdel 4044",
        "hgetex 4044 hincrby 4044 hincrbyfloat 4044 hkeys 8042 hlen 4042 hmget 4042 hmset 4044 hpersist 4044",
        "hpexpire 4044 hpexpireat 4044 hpexpiretime 4042 hpttl 4042 hrandfield 8042 hscan 8042 hset 4044 hsetex 4044",
        "hsetnx 4044 hstrlen 4042 httl 4042 hvals 8042 incr 4084 incrby 4084 incrbyfloat 4084 info 28000",
        "keys 28003 lastsave 26000 latency 8000 latency|doctor 2a000 latency|graph 2a000 latency|help 8000 latency|histogram 2a000 latency|history 2a000",
        "latency|latest 2a000 latency|reset 2a000 lcs 8082 lindex 8022 linsert 8024 llen 4022 lmove 8024 lmpop 8024",
        "lolwut 4002 lpop 4024 lpos 8022 lpush 4024 lpushx 4024 lrange 8022 lrem 8024 lset 8024",
        "ltrim 8024 memory 8000 memory|doctor 8000 memory|help 8000 memory|malloc-stats 8000 memory|purge 8000 memory|stats 8000 memory|usage 8002",
        "mget 4082 migrate 28005 module 8000 module|help 8000 module|list 2a000 module|load 2a000 module|loadex 2a000 module|unload 2a000",
        "monitor 2a000 move 4005 mset 8084 msetnx 8084 multi 84000 object 8000 object|encoding 8003 object|freq 8003",
        "object|help 8001 object|idletime 8003 object|refcount 8003 persist 4005 pexpire 4005 pexpireat 4005 pexpiretime 4003 pfadd 4204",
        "pfcount 8202 pfdebug 2a204 pfmerge 8204 pfselftest 2a200 ping 44000 psetex 8084 psubscribe 9000 psync 2a000",
        "pttl 4003 publish 5000 pubsub 8000 pubsub|channels 9000 pubsub|help 8000 pubsub|numpat 9000 pubsub|numsub 9000 pubsub|shardchannels 9000",
        "pubsub|shardnumsub 9000 punsubscribe 9000 quit 44000! randomkey 8003 readonly 44000 readwrite 44000 rename 8005 renamenx 4005",
        "replconf 2a000 replicaof 2a000 reset 44000! restore 28005 restore-asking 28005 role 26000 rpop 4024 rpoplpush 8024",
        "rpush 4024 rpushx 4024 sadd 400c save 2a000 scan 8003 scard 400a script 8000 script|debug 108000",
        "script|exists 108000 script|flush 108000 script|help 108000 script|kill 108000 script|load 108000 sdiff 800a sdiffstore 800c select 44000",
        "sentinel 2a000 sentinel|ckquorum 2a000 sentinel|config 2a000 sentinel|debug 2a000 sentinel|failover 2a000 sentinel|flushconfig 2a000 sentinel|get-master-addr-by-name 2a000 sentinel|help 8000",
        "sentinel|info-cache 2a000 sentinel|is-master-down-by-addr 2a000 sentinel|master 2a000 sentinel|masters 2a000 sentinel|monitor 2a000 sentinel|myid 2a000 sentinel|pending-scripts 2a000 sentinel|remove 2a000",
        "sentinel|replicas 2a000 sentinel|reset 2a000 sentinel|sentinels 2a000 sentinel|set 2a000 sentinel|simulate-failure 2a000 sentinel|slaves 2a000 set 8084 setbit 8104",
        "setex 8084 setnx 4084 setrange 8084 shutdown 2a000 sinter 800a sintercard 800a sinterstore 800c sismember 400a",
        "slaveof 2a000 slowlog 8000 slowlog|get 2a000 slowlog|help 8000 slowlog|len 2a000 slowlog|reset 2a000 smembers 800a smismember 400a",
        "smove 400c sort 2803c sort_ro 2803a spop 400c spublish 5000 srandmember 800a srem 400c sscan 800a",
        "ssubscribe 9000 strlen 4082 subscribe 9000 substr 8082 sunion 800a sunionstore 800c sunsubscribe 9000 swapdb 24005",
        "sync 2a000 time 4000 touch 4003 ttl 4003 type 4003 unlink 4005 unsubscribe 9000 unwatch 84000",
        "wait 58000 waitaof 58000 watch 84000 xack 4804 xadd 4804 xautoclaim 4804 xclaim 4804 xdel 4804",
        "xgroup 8000 xgroup|create 8804 xgroup|createconsumer 8804 xgroup|delconsumer 8804 xgroup|destroy 8804 xgroup|help 8800 xgroup|setid 8804 xinfo 8000",
        "xinfo|consumers 8802 xinfo|groups 8802 xinfo|help 8800 xinfo|stream 8802 xlen 4802 xpending 8802 xrange 8802 xread 18802",
        "xreadgroup 18804 xrevrange 8802 xsetid 4804 xtrim 8804 zadd 4014 zcard 4012 zcount 4012 zdiff 8012",
        "zdiffstore 8014 zincrby 4014 zinter 8012 zintercard 8012 zinterstore 8014 zlexcount 4012 zmpop 8014 zmscore 4012",
        "zpopmax 4014 zpopmin 4014 zrandmember 8012 zrange 8012 zrangebylex 8012 zrangebyscore 8012 zrangestore 8014 zrank 4012",
        "zrem 4014 zremrangebylex 8014 zremrangebyrank 8014 zremrangebyscore 8014 zrevrange 8012 zrevrangebylex 8012 zrevrangebyscore 8012 zrevrank 4012",
        "zscan 8012 zscore 4012 zunion 8012 zunionstore 8014",
    };

    /**
     * "<fullname>=<spec>;<spec>[/<getkeys-proc>]" where a spec is
     * "<flags><begin-search>:<find-keys>"; flags R=ACCESS I=INSERT D=DELETE
     * U=UPDATE N=NOT_KEY V=VARIABLE_FLAGS C=INCOMPLETE (or '-' for none),
     * begin-search "i<pos>" / "k<startfrom>,<keyword>" / "u",
     * find-keys "r<lastkey>,<step>,<limit>" / "n<keynumidx>,<step>,<firstkey>" / "u".
     */
    static final String[] KS_DATA = {
        "append=Ii1:r0,1,0 bitcount=Ri1:r0,1,0 bitfield=RUVi1:r0,1,0/bitfieldGetKeys bitfield_ro=Ri1:r0,1,0 bitop=Ui2:r0,1,0;Ri3:r-1,1,0",
        "bitpos=Ri1:r0,1,0 blmove=RDi1:r0,1,0;Ii2:r0,1,0 blmpop=RDi2:n0,1,1/blmpopGetKeys blpop=RDi1:r-2,1,0 brpop=RDi1:r-2,1,0",
        "brpoplpush=RDi1:r0,1,0;Ii2:r0,1,0 bzmpop=RDi2:n0,1,1/blmpopGetKeys bzpopmax=RDi1:r-2,1,0 bzpopmin=RDi1:r-2,1,0 copy=Ri1:r0,1,0;Ui2:r0,1,0",
        "decr=RUi1:r0,1,0 decrby=RUi1:r0,1,0 del=Di1:r-1,1,0 dump=Ri1:r0,1,0 eval=RUi2:n0,1,1/evalGetKeys eval_ro=Ri2:n0,1,1/evalGetKeys",
        "evalsha=RUi2:n0,1,1/evalGetKeys evalsha_ro=Ri2:n0,1,1/evalGetKeys exists=-i1:r-1,1,0 expire=Ui1:r0,1,0 expireat=Ui1:r0,1,0",
        "expiretime=Ri1:r0,1,0 fcall=RUi2:n0,1,1/functionGetKeys fcall_ro=Ri2:n0,1,1/functionGetKeys geoadd=Ui1:r0,1,0 geodist=Ri1:r0,1,0",
        "geohash=Ri1:r0,1,0 geopos=Ri1:r0,1,0 georadius=Ri1:r0,1,0;Uk6,STORE:r0,1,0;Uk6,STOREDIST:r0,1,0/georadiusGetKeys",
        "georadius_ro=Ri1:r0,1,0 georadiusbymember=Ri1:r0,1,0;Uk5,STORE:r0,1,0;Uk5,STOREDIST:r0,1,0/georadiusGetKeys georadiusbymember_ro=Ri1:r0,1,0",
        "geosearch=Ri1:r0,1,0 geosearchstore=Ui1:r0,1,0;Ri2:r0,1,0 get=Ri1:r0,1,0 getbit=Ri1:r0,1,0 getdel=RDi1:r0,1,0 getex=RUi1:r0,1,0",
        "getrange=Ri1:r0,1,0 getset=RUi1:r0,1,0 hdel=Di1:r0,1,0 hexists=-i1:r0,1,0 hexpire=Ui1:r0,1,0 hexpireat=Ui1:r0,1,0",
        "hexpiretime=Ri1:r0,1,0 hget=Ri1:r0,1,0 hgetall=Ri1:r0,1,0 hgetdel=RDi1:r0,1,0 hgetex=RUi1:r0,1,0 hincrby=RUi1:r0,1,0",
        "hincrbyfloat=RUi1:r0,1,0 hkeys=Ri1:r0,1,0 hlen=-i1:r0,1,0 hmget=Ri1:r0,1,0 hmset=Ui1:r0,1,0 hpersist=Ui1:r0,1,0",
        "hpexpire=Ui1:r0,1,0 hpexpireat=Ui1:r0,1,0 hpexpiretime=Ri1:r0,1,0 hpttl=Ri1:r0,1,0 hrandfield=Ri1:r0,1,0 hscan=Ri1:r0,1,0",
        "hset=Ui1:r0,1,0 hsetex=Ui1:r0,1,0 hsetnx=Ii1:r0,1,0 hstrlen=-i1:r0,1,0 httl=Ri1:r0,1,0 hvals=Ri1:r0,1,0 incr=RUi1:r0,1,0",
        "incrby=RUi1:r0,1,0 incrbyfloat=RUi1:r0,1,0 lcs=Ri1:r1,1,0 lindex=Ri1:r0,1,0 linsert=Ii1:r0,1,0 llen=-i1:r0,1,0 lmove=RDi1:r0,1,0;Ii2:r0,1,0",
        "lmpop=RDi1:n0,1,1/lmpopGetKeys lpop=RDi1:r0,1,0 lpos=Ri1:r0,1,0 lpush=Ii1:r0,1,0 lpushx=Ii1:r0,1,0 lrange=Ri1:r0,1,0",
        "lrem=Di1:r0,1,0 lset=Ui1:r0,1,0 ltrim=Di1:r0,1,0 memory|usage=-i2:r0,1,0 mget=Ri1:r-1,1,0 migrate=RDi3:r0,1,0;RDCk-2,KEYS:r-1,1,0/migrateGetKeys",
        "move=RUi1:r0,1,0 mset=Ui1:r-1,2,0 msetnx=Ii1:r-1,2,0 object|encoding=-i2:r0,1,0 object|freq=-i2:r0,1,0 object|idletime=-i2:r0,1,0",
        "object|refcount=-i2:r0,1,0 persist=Ui1:r0,1,0 pexpire=Ui1:r0,1,0 pexpireat=Ui1:r0,1,0 pexpiretime=Ri1:r0,1,0 pfadd=Ii1:r0,1,0",
        "pfcount=Ri1:r-1,1,0 pfdebug=Ri2:r0,1,0 pfmerge=RIi1:r0,1,0;Ri2:r-1,1,0 psetex=Ui1:r0,1,0 pttl=Ri1:r0,1,0 rename=RDi1:r0,1,0;Ui2:r0,1,0",
        "renamenx=RDi1:r0,1,0;Ii2:r0,1,0 restore=Ui1:r0,1,0 restore-asking=Ui1:r0,1,0 rpop=RDi1:r0,1,0 rpoplpush=RDi1:r0,1,0;Ii2:r0,1,0",
        "rpush=Ii1:r0,1,0 rpushx=Ii1:r0,1,0 sadd=Ii1:r0,1,0 scard=-i1:r0,1,0 sdiff=Ri1:r-1,1,0 sdiffstore=Ui1:r0,1,0;Ri2:r-1,1,0",
        "set=RUVi1:r0,1,0/setGetKeys setbit=RUi1:r0,1,0 setex=Ui1:r0,1,0 setnx=Ii1:r0,1,0 setrange=Ui1:r0,1,0 sinter=Ri1:r-1,1,0",
        "sintercard=Ri1:n0,1,1/sintercardGetKeys sinterstore=Ui1:r0,1,0;Ri2:r-1,1,0 sismember=-i1:r0,1,0 smembers=Ri1:r0,1,0",
        "smismember=Ri1:r0,1,0 smove=RDi1:r0,1,0;Ii2:r0,1,0 sort=Ri1:r0,1,0;Ru:u;Uu:u/sortGetKeys sort_ro=Ri1:r0,1,0;Ru:u/sortROGetKeys",
        "spop=RDi1:r0,1,0 spublish=Ni1:r0,1,0 srandmember=Ri1:r0,1,0 srem=Di1:r0,1,0 sscan=Ri1:r0,1,0 ssubscribe=Ni1:r-1,1,0",
        "strlen=-i1:r0,1,0 substr=Ri1:r0,1,0 sunion=Ri1:r-1,1,0 sunionstore=Ui1:r0,1,0;Ri2:r-1,1,0 sunsubscribe=Ni1:r-1,1,0",
        "touch=-i1:r-1,1,0 ttl=Ri1:r0,1,0 type=-i1:r0,1,0 unlink=Di1:r-1,1,0 watch=-i1:r-1,1,0 xack=Ui1:r0,1,0 xadd=Ui1:r0,1,0",
        "xautoclaim=Di1:r0,1,0 xclaim=Ui1:r0,1,0 xdel=Di1:r0,1,0 xgroup|create=Ii2:r0,1,0 xgroup|createconsumer=Ii2:r0,1,0",
        "xgroup|delconsumer=Di2:r0,1,0 xgroup|destroy=Di2:r0,1,0 xgroup|setid=Ui2:r0,1,0 xinfo|consumers=Ri2:r0,1,0 xinfo|groups=Ri2:r0,1,0",
        "xinfo|stream=Ri2:r0,1,0 xlen=-i1:r0,1,0 xpending=Ri1:r0,1,0 xrange=Ri1:r0,1,0 xread=Rk1,STREAMS:r-1,1,2/xreadGetKeys",
        "xreadgroup=Rk4,STREAMS:r-1,1,2/xreadGetKeys xrevrange=Ri1:r0,1,0 xsetid=Ui1:r0,1,0 xtrim=Di1:r0,1,0 zadd=Ui1:r0,1,0",
        "zcard=-i1:r0,1,0 zcount=Ri1:r0,1,0 zdiff=Ri1:n0,1,1/zunionInterDiffGetKeys zdiffstore=Ui1:r0,1,0;Ri2:n0,1,1/zunionInterDiffStoreGetKeys",
        "zincrby=RUi1:r0,1,0 zinter=Ri1:n0,1,1/zunionInterDiffGetKeys zintercard=Ri1:n0,1,1/zunionInterDiffGetKeys zinterstore=Ui1:r0,1,0;Ri2:n0,1,1/zunionInterDiffStoreGetKeys",
        "zlexcount=Ri1:r0,1,0 zmpop=RDi1:n0,1,1/zmpopGetKeys zmscore=Ri1:r0,1,0 zpopmax=RDi1:r0,1,0 zpopmin=RDi1:r0,1,0 zrandmember=Ri1:r0,1,0",
        "zrange=Ri1:r0,1,0 zrangebylex=Ri1:r0,1,0 zrangebyscore=Ri1:r0,1,0 zrangestore=Ui1:r0,1,0;Ri2:r0,1,0 zrank=Ri1:r0,1,0",
        "zrem=Di1:r0,1,0 zremrangebylex=Di1:r0,1,0 zremrangebyrank=Di1:r0,1,0 zremrangebyscore=Di1:r0,1,0 zrevrange=Ri1:r0,1,0",
        "zrevrangebylex=Ri1:r0,1,0 zrevrangebyscore=Ri1:r0,1,0 zrevrank=Ri1:r0,1,0 zscan=Ri1:r0,1,0 zscore=Ri1:r0,1,0 zunion=Ri1:n0,1,1/zunionInterDiffGetKeys",
        "zunionstore=Ui1:r0,1,0;Ri2:n0,1,1/zunionInterDiffStoreGetKeys",
    };
}
