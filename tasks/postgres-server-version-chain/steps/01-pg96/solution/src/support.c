/* support.c - odds and ends the catalog and executor call into:
 * the pg_type scan, SQL/JSON constructors, jsonb_each expansion, binary
 * parameter/result encoding and the replication command surface. */
#include "engine.h"
#include "stmt.h"

/* ------------------------------------------------------------- pg_type */

/* pg_type.typcategory groups the types the way the parser's implicit-cast
 * rules do.  It is carried per row in the table below (the .cat field); the
 * fallback for a row that leaves it 0 is 'A' for arrays, 'R' for range and
 * multirange types and 'U' for everything else, which is where PostgreSQL
 * puts bytea, uuid, xml, json, jsonb, jsonpath and pg_lsn. */

void type_catalog_scan(QueryResult *r, Oid nsp)
{
    static const struct { Oid oid; const char *name; char typtype; int len; Oid elem; Oid arr;
                          char cat; } t[] = {
        {16,"bool",'b',1,0,1000,'B'}, {17,"bytea",'b',-1,0,1001,'U'}, {18,"char",'b',1,0,1002,'S'},
        {19,"name",'b',64,0,1003,'S'}, {20,"int8",'b',8,0,1016,'N'}, {21,"int2",'b',2,0,1005,'N'},
        {23,"int4",'b',4,0,1007,'N'}, {25,"text",'b',-1,0,1009,'S'}, {26,"oid",'b',4,0,1028,'N'},
        {114,"json",'b',-1,0,199,'U'}, {142,"xml",'b',-1,0,143,'U'},
        {700,"float4",'b',4,0,1021,'N'}, {701,"float8",'b',8,0,1022,'N'},
        {705,"unknown",'p',-2,0,0,'X'}, {790,"money",'b',8,0,791,'N'},
        {1017,"_point",'b',-1,600,0,'A'},
        {1042,"bpchar",'b',-1,0,1014,'S'}, {1043,"varchar",'b',-1,0,1015,'S'},
        {1082,"date",'b',4,0,1182,'D'}, {1083,"time",'b',8,0,1183,'D'},
        {1114,"timestamp",'b',8,0,1115,'D'}, {1184,"timestamptz",'b',8,0,1185,'D'},
        {1186,"interval",'b',16,0,1187,'T'}, {1266,"timetz",'b',12,0,1270,'D'},
        {1700,"numeric",'b',-1,0,1231,'N'}, {2205,"regclass",'b',4,0,2210,'N'},
        {2206,"regtype",'b',4,0,2211,'N'}, {24,"regproc",'b',4,0,1008,'N'},
        {2950,"uuid",'b',16,0,2951,'U'}, {3802,"jsonb",'b',-1,0,3807,'U'},
        {4072,"jsonpath",'b',-1,0,4073,'U'},
        {3904,"int4range",'r',-1,23,3905}, {3906,"numrange",'r',-1,1700,3907},
        {3908,"tsrange",'r',-1,1114,3909}, {3910,"tstzrange",'r',-1,1184,3911},
        {3912,"daterange",'r',-1,1082,3913}, {3926,"int8range",'r',-1,20,3927},
#if PGVC_VN >= 140
        {4451,"int4multirange",'m',-1,3904,6150}, {4532,"nummultirange",'m',-1,3906,6151},
        {4533,"tsmultirange",'m',-1,3908,6152}, {4534,"tstzmultirange",'m',-1,3910,6153},
        {4535,"datemultirange",'m',-1,3912,6155}, {4536,"int8multirange",'m',-1,3926,6157},
#endif
        {1000,"_bool",'b',-1,16,0,'A'}, {1005,"_int2",'b',-1,21,0,'A'}, {1007,"_int4",'b',-1,23,0,'A'},
        {1009,"_text",'b',-1,25,0,'A'}, {1016,"_int8",'b',-1,20,0,'A'}, {1021,"_float4",'b',-1,700,0,'A'},
        {1022,"_float8",'b',-1,701,0,'A'}, {1231,"_numeric",'b',-1,1700,0,'A'},
        {1015,"_varchar",'b',-1,1043,0,'A'}, {1014,"_bpchar",'b',-1,1042,0,'A'},
        {2951,"_uuid",'b',-1,2950,0,'A'}, {3807,"_jsonb",'b',-1,3802,0,'A'}, {199,"_json",'b',-1,114,0,'A'},
        {2249,"record",'p',-1,0,2287,'P'}, {2278,"void",'p',4,0,0,'P'},
        {600,"point",'b',16,0,1017,'G'}, {601,"lseg",'b',32,0,1018,'G'},
        {602,"path",'b',-1,0,1019,'G'}, {603,"box",'b',32,0,1020,'G'},
        {604,"polygon",'b',-1,0,1027,'G'}, {628,"line",'b',24,0,629,'G'},
        {718,"circle",'b',24,0,719,'G'},
        {28,"xid",'b',4,0,1011,'U'}, {1011,"_xid",'b',-1,28,0,'A'},
        {2970,"txid_snapshot",'b',-1,0,2949,'U'}, {2949,"_txid_snapshot",'b',-1,2970,0,'A'},
#if PGVC_VN >= 130
        {5069,"xid8",'b',8,0,271,'U'}, {271,"_xid8",'b',-1,5069,0,'A'},
        {5038,"pg_snapshot",'b',-1,0,5039,'U'}, {5039,"_pg_snapshot",'b',-1,5038,0,'A'},
#endif
        {0,NULL,0,0,0,0}
    };
    for (int i = 0; t[i].name; i++) {
        Datum *row = xmalloc(10 * sizeof(Datum));
        row[0] = d_int(OIDOID, t[i].oid);
        row[1] = d_text(NAMEOID, t[i].name);
        row[2] = d_int(OIDOID, nsp);
        char ty[2] = {t[i].typtype, 0};
        row[3] = d_text(CHAROID, ty);
        row[4] = d_int(INT2OID, t[i].len);
        row[5] = d_int(OIDOID, t[i].elem);
        row[6] = d_int(OIDOID, t[i].arr);
        row[7] = d_int(OIDOID, 0);
        /* typcategory: the class PostgreSQL files the type under */
        char cat = t[i].cat;
        if (!cat) {
            if (t[i].name[0] == '_') cat = 'A';
            else if (t[i].typtype == 'r') cat = 'R';
            else if (t[i].typtype == 'm') cat = 'R';
            else cat = 'U';
        }
        char cb[2] = {cat, 0};
        row[8] = d_text(CHAROID, cb);
#if PGVC_VN >= 140
        {
            const char *h = "-";
            if (t[i].name[0] == '_') h = "array_subscript_handler";
            else if (!strcmp(t[i].name, "jsonb")) h = "jsonb_subscript_handler";
            row[9] = reg_make(NULL, REGPROCOID, 0, h);
        }
#endif
        qr_addrow(r, row);
    }
}

/* ------------------------------------------------------------- pg_proc
 * The routines a query can see by name.  prokind matters from PostgreSQL 11
 * on; before that the same information lived in proisagg/proiswindow. */
const ProcDef *builtin_procs(int *n)
{
    static const ProcDef p[] = {
        {"abs",'f',0,0}, {"array_length",'f',0,0}, {"btrim",'f',0,0},
        {"ceil",'f',0,0}, {"coalesce",'f',0,0}, {"concat",'f',0,0},
        {"date_part",'f',0,0}, {"date_trunc",'f',0,0}, {"extract",'f',0,0},
        {"floor",'f',0,0}, {"format",'f',0,0}, {"generate_series",'f',0,0},
        {"greatest",'f',0,0}, {"initcap",'f',0,0}, {"jsonb_build_object",'f',0,0},
        {"jsonb_set",'f',0,0}, {"least",'f',0,0}, {"length",'f',0,0},
        {"lower",'f',0,0}, {"lpad",'f',0,0}, {"ltrim",'f',0,0},
        {"now",'f',0,0}, {"nullif",'f',0,0}, {"position",'f',0,0},
        {"power",'f',0,0}, {"random",'f',0,0,"","double precision"},
        {"regexp_replace",'f',0,140},
        {"repeat",'f',0,0}, {"replace",'f',0,0}, {"round",'f',0,0},
        {"rpad",'f',0,0}, {"rtrim",'f',0,0}, {"split_part",'f',0,0},
        {"sqrt",'f',0,0}, {"strpos",'f',0,0}, {"substr",'f',0,0},
        {"substring",'f',0,0}, {"to_char",'f',0,0}, {"to_date",'f',0,0},
        {"to_timestamp",'f',0,0}, {"trim",'f',0,0}, {"trunc",'f',0,0},
        {"upper",'f',0,0}, {"version",'f',0,0},
        {"md5",'f',0,0}, {"to_regclass",'f',0,0}, {"to_regtype",'f',0,0},
        {"gen_random_uuid",'f',130,0,"","uuid"}, {"setseed",'f',0,0,"double precision","void"},
        {"pg_get_expr",'f',0,0}, {"pg_get_indexdef",'f',0,0},
        {"pg_get_constraintdef",'f',0,0}, {"pg_get_viewdef",'f',0,0},
        {"pg_get_function_arguments",'f',0,0}, {"pg_get_function_result",'f',0,0},
        {"pg_get_function_identity_arguments",'f',0,0},
        {"pg_get_serial_sequence",'f',0,0}, {"pg_typeof",'f',0,0},
        {"pg_relation_size",'f',0,0}, {"pg_total_relation_size",'f',0,0},
        {"pg_size_pretty",'f',0,0}, {"pg_backend_pid",'f',0,0},
        {"pg_get_userbyid",'f',0,0}, {"pg_encoding_to_char",'f',0,0},
        {"pg_table_is_visible",'f',0,0}, {"pg_type_is_visible",'f',0,0},
        {"pg_is_in_recovery",'f',0,0}, {"pg_sleep",'f',0,0},
        {"pg_logical_slot_get_changes",'f',0,0},
        {"pg_logical_slot_peek_changes",'f',0,0},
        {"pg_create_logical_replication_slot",'f',0,96,
         "slot_name name, plugin name, OUT slot_name text, OUT xlog_position pg_lsn","record"},
        {"pg_create_logical_replication_slot",'f',100,110,
         "slot_name name, plugin name, temporary boolean DEFAULT false, "
         "OUT slot_name text, OUT lsn pg_lsn","record"},
        {"pg_create_logical_replication_slot",'f',120,130,
         "slot_name name, plugin name, temporary boolean DEFAULT false, "
         "OUT slot_name name, OUT lsn pg_lsn","record"},
        {"pg_create_logical_replication_slot",'f',140,160,
         "slot_name name, plugin name, temporary boolean DEFAULT false, "
         "twophase boolean DEFAULT false, OUT slot_name name, OUT lsn pg_lsn","record"},
        {"pg_create_logical_replication_slot",'f',170,0,
         "slot_name name, plugin name, temporary boolean DEFAULT false, "
         "twophase boolean DEFAULT false, failover boolean DEFAULT false, "
         "OUT slot_name name, OUT lsn pg_lsn","record"},
        {"pg_create_physical_replication_slot",'f',0,96,
         "slot_name name, immediately_reserve boolean DEFAULT false, "
         "OUT slot_name text, OUT xlog_position pg_lsn","record"},
        {"pg_create_physical_replication_slot",'f',100,110,
         "slot_name name, immediately_reserve boolean DEFAULT false, "
         "temporary boolean DEFAULT false, OUT slot_name text, OUT lsn pg_lsn","record"},
        {"pg_create_physical_replication_slot",'f',120,0,
         "slot_name name, immediately_reserve boolean DEFAULT false, "
         "temporary boolean DEFAULT false, OUT slot_name name, OUT lsn pg_lsn","record"},
        {"pg_drop_replication_slot",'f',0,0,"name","void"},
        {"pg_replication_slot_advance",'f',110,0,
         "slot_name name, upto_lsn pg_lsn, OUT slot_name name, OUT end_lsn pg_lsn","record"},
        {"factorial",'f',0,0},
#if PGVC_VN >= 150
        /* PostgreSQL 15's regexp family: the argument count and result type are
         * carried per row, so pg_proc shows one row per signature */
        {"regexp_count",'f',150,0,.nargs1=3,.rettype=INT4OID},
        {"regexp_count",'f',150,0,.nargs1=4,.rettype=INT4OID},
        {"regexp_count",'f',150,0,.nargs1=5,.rettype=INT4OID},
        {"regexp_instr",'f',150,0,.nargs1=3,.rettype=INT4OID},
        {"regexp_instr",'f',150,0,.nargs1=4,.rettype=INT4OID},
        {"regexp_instr",'f',150,0,.nargs1=5,.rettype=INT4OID},
        {"regexp_instr",'f',150,0,.nargs1=6,.rettype=INT4OID},
        {"regexp_instr",'f',150,0,.nargs1=7,.rettype=INT4OID},
        {"regexp_instr",'f',150,0,.nargs1=8,.rettype=INT4OID},
        {"regexp_like",'f',150,0,.nargs1=3,.rettype=BOOLOID},
        {"regexp_like",'f',150,0,.nargs1=4,.rettype=BOOLOID},
        {"regexp_substr",'f',150,0,.nargs1=3,.rettype=TEXTOID},
        {"regexp_substr",'f',150,0,.nargs1=4,.rettype=TEXTOID},
        {"regexp_substr",'f',150,0,.nargs1=5,.rettype=TEXTOID},
        {"regexp_substr",'f',150,0,.nargs1=6,.rettype=TEXTOID},
        {"regexp_substr",'f',150,0,.nargs1=7,.rettype=TEXTOID},
        {"range_agg",'a',140,0,.nargs1=2,.rettype=4537,.arg0=4537},
        {"range_agg",'a',140,0,.nargs1=2,.rettype=4537,.arg0=3831},
        {"range_intersect_agg",'a',140,0,.nargs1=2,.rettype=4537,.arg0=4537},
        {"range_intersect_agg",'a',140,0,.nargs1=2,.rettype=3831,.arg0=3831},
        {"regexp_replace",'f',150,0,.nargs1=4,.rettype=TEXTOID},
        {"regexp_replace",'f',150,0,.nargs1=5,.rettype=TEXTOID},
        {"regexp_replace",'f',150,0,.nargs1=5,.rettype=TEXTOID},
        {"regexp_replace",'f',150,0,.nargs1=6,.rettype=TEXTOID},
        {"regexp_replace",'f',150,0,.nargs1=7,.rettype=TEXTOID},
#endif
        {"pg_start_backup",'f',0,140}, {"pg_stop_backup",'f',0,140},
        {"pg_is_in_backup",'f',0,140}, {"pg_backup_start_time",'f',0,140},
        {"pg_backup_start",'f',150,0}, {"pg_backup_stop",'f',150,0},
        {"to_regprocedure",'f',0,0}, {"has_function_privilege",'f',0,0},
        {"pg_get_wal_replay_pause_state",'f',140,0},
        {"pg_column_compression",'f',140,0},
        {"array_subscript_handler",'f',140,0}, {"jsonb_subscript_handler",'f',140,0},
        {"bit_count",'f',140,0}, {"trim_array",'f',140,0}, {"unistr",'f',140,0},
        {"date_bin",'f',140,0}, {"string_to_table",'f',140,0},
#if PGVC_VN < 150
        {"range_agg",'a',140,0}, {"range_intersect_agg",'a',140,0},
#endif
        {"min_scale",'f',130,0}, {"trim_scale",'f',130,0},
        {"gcd",'f',130,0}, {"lcm",'f',130,0},
        {"pg_snapshot_in",'f',130,0}, {"pg_snapshot_out",'f',130,0},
        {"pg_snapshot_recv",'f',130,0}, {"pg_snapshot_send",'f',130,0},
        {"pg_snapshot_xip",'f',130,0}, {"pg_snapshot_xmax",'f',130,0},
        {"pg_snapshot_xmin",'f',130,0},
        {"pg_current_xact_id",'f',130,0}, {"pg_current_xact_id_if_assigned",'f',130,0},
        {"pg_visible_in_snapshot",'f',130,0}, {"pg_xact_status",'f',100,0},
        /* aggregates */
        {"array_agg",'a',0,0}, {"avg",'a',0,0}, {"count",'a',0,0}, {"count",'a',0,0},
        {"max",'a',0,0}, {"min",'a',0,0}, {"string_agg",'a',0,0}, {"sum",'a',0,0},
        {"bool_and",'a',0,0}, {"bool_or",'a',0,0}, {"every",'a',0,0},
        {"json_agg",'a',0,0}, {"jsonb_agg",'a',0,0},
        {"json_object_agg",'a',0,0}, {"jsonb_object_agg",'a',0,0},
        {"stddev",'a',0,0}, {"variance",'a',0,0},
        {"rank",'a',0,0}, {"dense_rank",'a',0,0},
        {"percent_rank",'a',0,0}, {"cume_dist",'a',0,0},
        {"mode",'a',0,0}, {"percentile_cont",'a',0,0}, {"percentile_disc",'a',0,0},
        /* window functions */
        {"rank",'w',0,0}, {"dense_rank",'w',0,0}, {"row_number",'w',0,0},
        {"percent_rank",'w',0,0}, {"cume_dist",'w',0,0}, {"ntile",'w',0,0},
        {"lag",'w',0,0}, {"lead",'w',0,0}, {"first_value",'w',0,0},
        {"last_value",'w',0,0}, {"nth_value",'w',0,0},
        /* the write-ahead log function family was renamed in PostgreSQL 10 */
        {"pg_current_xlog_location",'f',0,96},
        {"pg_current_xlog_insert_location",'f',0,96},
        {"pg_current_xlog_flush_location",'f',0,96},
        {"pg_switch_xlog",'f',0,96},
        {"pg_xlogfile_name",'f',0,96},
        {"pg_xlogfile_name_offset",'f',0,96},
        {"pg_xlog_location_diff",'f',0,96},
        {"pg_last_xlog_receive_location",'f',0,96},
        {"pg_last_xlog_replay_location",'f',0,96},
        {"pg_is_xlog_replay_paused",'f',0,96},
        {"pg_xlog_replay_pause",'f',0,96},
        {"pg_xlog_replay_resume",'f',0,96},
        {"pg_current_wal_lsn",'f',100,0},
        {"pg_current_wal_insert_lsn",'f',100,0},
        {"pg_current_wal_flush_lsn",'f',100,0},
        {"pg_switch_wal",'f',100,0},
        {"pg_walfile_name",'f',100,0},
        {"pg_walfile_name_offset",'f',100,0},
        {"pg_wal_lsn_diff",'f',100,0},
        {"pg_last_wal_receive_lsn",'f',100,0},
        {"pg_last_wal_replay_lsn",'f',100,0},
        {"pg_is_wal_replay_paused",'f',100,0},
        {"pg_wal_replay_pause",'f',100,0},
        {"pg_wal_replay_resume",'f',100,0},
        /* PostgreSQL 10 additions */
        {"pg_current_logfile",'f',100,0}, {"pg_current_logfile",'f',100,0},
        {"pg_get_publication_tables",'f',100,0},
        {"pg_get_partition_constraintdef",'f',100,0},
        {"pg_partition_tree",'f',120,0},
        {"pg_partition_ancestors",'f',120,0},
        {"pg_partition_root",'f',120,0},
        {"satisfies_hash_partition",'f',110,0},
        {"sha224",'f',110,0}, {"sha256",'f',110,0},
        {"sha384",'f',110,0}, {"sha512",'f',110,0},
        {"websearch_to_tsquery",'f',110,0},
        {"jsonb_to_tsvector",'f',110,0}, {"json_to_tsvector",'f',110,0},
        {"jsonb_path_exists",'f',120,0}, {"jsonb_path_exists_opr",'f',120,0},
        {"jsonb_path_match",'f',120,0}, {"jsonb_path_match_opr",'f',120,0},
        {"jsonb_path_query",'f',120,0}, {"jsonb_path_query_array",'f',120,0},
        {"jsonb_path_query_first",'f',120,0},
        {"jsonb_path_exists_tz",'f',130,0}, {"jsonb_path_match_tz",'f',130,0},
        {"jsonb_path_query_tz",'f',130,0}, {"jsonb_path_query_array_tz",'f',130,0},
        {"jsonb_path_query_first_tz",'f',130,0},
        /* PostgreSQL 16 additions */
        {"any_value",'a',160,0,"anyelement","anyelement"},
        {"array_sample",'f',160,0,"anyarray, integer","anyarray"},
        {"array_shuffle",'f',160,0,"anyarray","anyarray"},
        {"random_normal",'f',160,0,
         "mean double precision DEFAULT 0, stddev double precision DEFAULT 1",
         "double precision"},
        {"erf",'f',160,0,"double precision","double precision"},
        {"erfc",'f',160,0,"double precision","double precision"},
        {"pg_input_is_valid",'f',160,0,"text, text","boolean"},
        {"pg_input_error_info",'f',160,0,
         "value text, type_name text, OUT message text, OUT detail text, "
         "OUT hint text, OUT sql_error_code text","record"},
        {"pg_split_walfile_name",'f',160,0,
         "file_name text, OUT segment_number numeric, OUT timeline_id bigint","record"},
        {"pg_log_standby_snapshot",'f',160,0,"","pg_lsn"},
        {"system_user",'f',160,0,"","text"},
        {"date_add",'f',160,0,"timestamp with time zone, interval",
         "timestamp with time zone"},
        {"date_add",'f',160,0,"timestamp with time zone, interval, text",
         "timestamp with time zone"},
        {"date_subtract",'f',160,0,"timestamp with time zone, interval",
         "timestamp with time zone"},
        {"date_subtract",'f',160,0,"timestamp with time zone, interval, text",
         "timestamp with time zone"},
        {"pg_stat_get_io",'f',160,0,"","SETOF record"},
        {"pg_stat_get_backend_subxact",'f',160,0,
         "bid integer, OUT subxact_count integer, OUT subxact_overflowed boolean","record"},
        {"json_agg_strict",'a',160,0,"anyelement","json"},
        {"jsonb_agg_strict",'a',160,0,"anyelement","jsonb"},
        {"json_object_agg_strict",'a',160,0,"\"any\", \"any\"","json"},
        {"jsonb_object_agg_strict",'a',160,0,"\"any\", \"any\"","jsonb"},
        {"json_object_agg_unique",'a',160,0,"\"any\", \"any\"","json"},
        {"jsonb_object_agg_unique",'a',160,0,"\"any\", \"any\"","jsonb"},
        {"json_object_agg_unique_strict",'a',160,0,"\"any\", \"any\"","json"},
        {"jsonb_object_agg_unique_strict",'a',160,0,"\"any\", \"any\"","jsonb"},
        {"json_object_agg_unique_transfn",'f',160,0,"internal, \"any\", \"any\"","internal"},
        {"jsonb_object_agg_unique_transfn",'f',160,0,"internal, \"any\", \"any\"","internal"},
        {"json_object_agg_unique_strict_transfn",'f',160,0,"internal, \"any\", \"any\"","internal"},
        {"jsonb_object_agg_unique_strict_transfn",'f',160,0,"internal, \"any\", \"any\"","internal"},
        /* PostgreSQL 17 additions */
        {"to_bin",'f',170,0,"bigint","text"},
        {"to_bin",'f',170,0,"integer","text"},
        {"to_oct",'f',170,0,"bigint","text"},
        {"to_oct",'f',170,0,"integer","text"},
        {"to_hex",'f',0,0,"bigint","text"},
        {"to_hex",'f',0,0,"integer","text"},
        {"to_regtypemod",'f',170,0,"text","integer"},
        {"unicode_assigned",'f',170,0,"text","boolean"},
        {"unicode_version",'f',170,0,"","text"},
        {"icu_unicode_version",'f',170,0,"","text"},
        {"pg_column_toast_chunk_id",'f',170,0,"\"any\"","oid"},
        {"uuid_extract_timestamp",'f',170,0,"uuid","timestamp with time zone"},
        {"uuid_extract_version",'f',170,0,"uuid","smallint"},
        {"pg_sync_replication_slots",'f',170,0,"","void"},
        {"pg_basetype",'f',170,0,"regtype","regtype"},
        {"random",'f',170,0,"min integer, max integer","integer"},
        {"random",'f',170,0,"min bigint, max bigint","bigint"},
        {"random",'f',170,0,"min numeric, max numeric","numeric"},
        {"pg_get_replication_slots",'f',170,0,"","SETOF record"},
        {"pg_copy_logical_replication_slot",'f',120,0,"","record"},
        {"pg_copy_logical_replication_slot",'f',120,0,"","record"},
        {"pg_copy_logical_replication_slot",'f',120,0,"","record"},
        {"pg_copy_physical_replication_slot",'f',120,0,"","record"},
        {"pg_copy_physical_replication_slot",'f',120,0,"","record"},
        {"pg_stat_get_replication_slot",'f',140,0,"","record"},
        {"pg_stat_reset_replication_slot",'f',140,0,"text","void"},
        /* PostgreSQL 18 additions */
        {"uuidv4",'f',180,0,"","uuid"},
        {"uuidv7",'f',180,0,"","uuid"},
        {"uuidv7",'f',180,0,"shift interval","uuid"},
        {"array_reverse",'f',180,0,"anyarray","anyarray"},
        {"array_sort",'f',180,0,"anyarray","anyarray"},
        {"array_sort",'f',180,0,"\"array\" anyarray, descending boolean","anyarray"},
        {"array_sort",'f',180,0,
         "\"array\" anyarray, descending boolean, nulls_first boolean","anyarray"},
        {"crc32",'f',180,0,"bytea","bigint"},
        {"crc32c",'f',180,0,"bytea","bigint"},
        {"casefold",'f',180,0,"text","text"},
        {"xmltext",'f',170,0,"text","xml"},
        /* the uuid type's own support routines */
        {"uuid_cmp",'f',0,0,"uuid, uuid","integer"},
        {"uuid_eq",'f',0,0,"uuid, uuid","boolean"},
        {"uuid_ge",'f',0,0,"uuid, uuid","boolean"},
        {"uuid_gt",'f',0,0,"uuid, uuid","boolean"},
        {"uuid_hash",'f',0,0,"uuid","integer"},
        {"uuid_hash_extended",'f',110,0,"uuid, bigint","bigint"},
        {"uuid_in",'f',0,0,"cstring","uuid"},
        {"uuid_le",'f',0,0,"uuid, uuid","boolean"},
        {"uuid_lt",'f',0,0,"uuid, uuid","boolean"},
        {"uuid_ne",'f',0,0,"uuid, uuid","boolean"},
        {"uuid_out",'f',0,0,"uuid","cstring"},
        {"uuid_recv",'f',0,0,"internal","uuid"},
        {"uuid_send",'f',0,0,"uuid","bytea"},
        {"uuid_sortsupport",'f',0,0,"internal","void"},
        {"uuid_skipsupport",'f',180,0,"internal","void"},
        {NULL,0,0,0,NULL,NULL}
    };
    int k = 0;
    while (p[k].name) k++;
    *n = k;
    return p;
}

/* ---------------------------------------------------------- pg_operator */
void operator_catalog_scan(Session *s, QueryResult *r, Oid nsp)
{
    (void)s;
    static const struct { const char *nm; Oid l, rt, res; int minvn; } ops[] = {
        {"=",   INT4OID, INT4OID, BOOLOID, 0}, {"<",  INT4OID, INT4OID, BOOLOID, 0},
        {">",   INT4OID, INT4OID, BOOLOID, 0}, {"+",  INT4OID, INT4OID, INT4OID, 0},
        {"-",   INT4OID, INT4OID, INT4OID, 0}, {"*",  INT4OID, INT4OID, INT4OID, 0},
        {"/",   INT4OID, INT4OID, INT4OID, 0}, {"||", TEXTOID, TEXTOID, TEXTOID, 0},
        {"@>",  JSONBOID, JSONBOID, BOOLOID, 0}, {"<@", JSONBOID, JSONBOID, BOOLOID, 0},
        {"?",   JSONBOID, TEXTOID, BOOLOID, 0},
        {"->",  JSONBOID, TEXTOID, JSONBOID, 0}, {"->>", JSONBOID, TEXTOID, TEXTOID, 0},
        {"#>",  JSONBOID, TEXTARRAYOID, JSONBOID, 0},
        {"#>>", JSONBOID, TEXTARRAYOID, TEXTOID, 0},
        {"@?",  JSONBOID, JSONPATHOID, BOOLOID, 120},
        {"@@",  JSONBOID, JSONPATHOID, BOOLOID, 120},
        {"-",   2950 /*pg_lsn*/, 2950, NUMERICOID, 0},
        {"@>",  BOXOID, BOXOID, BOOLOID, 0}, {"@>", BOXOID, POINTOID, BOOLOID, 0},
        {"<@",  BOXOID, BOXOID, BOOLOID, 0}, {"<@", POINTOID, BOXOID, BOOLOID, 0},
        {"@>",  CIRCLEOID, CIRCLEOID, BOOLOID, 0}, {"@>", CIRCLEOID, POINTOID, BOOLOID, 0},
        {"<@",  CIRCLEOID, CIRCLEOID, BOOLOID, 0}, {"<@", POINTOID, CIRCLEOID, BOOLOID, 0},
        {"@",   BOXOID, BOXOID, BOOLOID, 0}, {"~", BOXOID, BOXOID, BOOLOID, 0},
        {"@",   POINTOID, BOXOID, BOOLOID, 0}, {"@", CIRCLEOID, CIRCLEOID, BOOLOID, 0},
        {"~",   CIRCLEOID, CIRCLEOID, BOOLOID, 0},
        {"!",   INT8OID, 0, NUMERICOID, 0},
        {"!!",  0, INT8OID, NUMERICOID, 0},
        {"!!",  0, 3615 /*tsquery*/, 3615, 140},
        {NULL,0,0,0,0}
    };
    for (int i = 0; ops[i].nm; i++) {
        if (ops[i].minvn && PGVC_VN < ops[i].minvn) continue;
#if PGVC_VN >= 140
        /* the factorial operators went away in PostgreSQL 14; the tsquery
         * negation operator keeps the "!!" name */
        if (!strcmp(ops[i].nm, "!") ||
            (!strcmp(ops[i].nm, "!!") && ops[i].rt == INT8OID)) continue;
        /* the geometric "@" and "~" containment operators went too */
        if ((!strcmp(ops[i].nm, "@") || !strcmp(ops[i].nm, "~")) &&
            (ops[i].l == BOXOID || ops[i].l == POINTOID || ops[i].l == CIRCLEOID)) continue;
#endif
        Datum *row = xmalloc(7 * sizeof(Datum));
        row[0] = d_int(OIDOID, (Oid)(900 + i));
        row[1] = d_text(NAMEOID, ops[i].nm);
        row[2] = d_int(OIDOID, nsp);
        row[3] = d_text(CHAROID, !ops[i].l ? "l" : !ops[i].rt ? "r" : "b");
        row[4] = d_int(OIDOID, ops[i].l);
        row[5] = d_int(OIDOID, ops[i].rt);
        row[6] = d_int(OIDOID, ops[i].res);
        qr_addrow(r, row);
    }
}

/* ---------------------------------------------------------- time zones
 * Named zones come from the operating system's IANA files (tzdata.c).  What
 * is left here is the fixed-offset half of PostgreSQL's zone handling: the
 * abbreviations its own table carries, and the bare numeric specification. */
typedef struct { const char *ab; int off; } TzAbbrev;
static const TzAbbrev tzabbrevs[] = {
    {"UTC",0}, {"GMT",0}, {"UT",0}, {"Z",0}, {"ZULU",0},
    {"EST",-5*3600}, {"EDT",-4*3600}, {"CST",-6*3600}, {"CDT",-5*3600},
    {"MST",-7*3600}, {"MDT",-6*3600}, {"PST",-8*3600}, {"PDT",-7*3600},
    {"AST",-4*3600}, {"ADT",-3*3600}, {"NST",-3*3600-1800},
    {"CET",1*3600}, {"CEST",2*3600}, {"EET",2*3600}, {"EEST",3*3600},
    {"WET",0}, {"WEST",1*3600}, {"BST",1*3600},
    {"MSK",3*3600}, {"JST",9*3600}, {"KST",9*3600}, {"IST",5*3600+1800},
    {"HKT",8*3600}, {"SGT",8*3600}, {"AEST",10*3600}, {"AEDT",11*3600},
    {"ACST",9*3600+1800}, {"AWST",8*3600}, {"NZST",12*3600}, {"NZDT",13*3600},
    {NULL,0}
};

static bool tz_ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

/* The fixed offset of a zone abbreviation, as PostgreSQL's own table has it. */
bool tz_abbrev_offset(const char *ab, int *off)
{
    for (int i = 0; tzabbrevs[i].ab; i++)
        if (tz_ci_eq(tzabbrevs[i].ab, ab)) { *off = tzabbrevs[i].off; return true; }
    return false;
}

bool pgzone_resolve(const char *name, PgZone *out)
{
    if (!name) return false;
    while (*name == ' ') name++;
    memset(out, 0, sizeof *out);
    /* A bare numeric specification uses the POSIX sign convention: a leading
     * "+" means west of Greenwich, matching pg_tzset_offset(). */
    if (*name == '+' || *name == '-') {
        int sign = (*name == '+') ? -1 : 1;
        const char *p = name + 1;
        if (!isdigit((unsigned char)*p)) return false;
        int hh = 0, mm = 0, ss = 0;
        while (isdigit((unsigned char)*p)) hh = hh * 10 + (*p++ - '0');
        if (*p == ':') { p++; if (!isdigit((unsigned char)*p)) return false;
                         while (isdigit((unsigned char)*p)) mm = mm * 10 + (*p++ - '0'); }
        if (*p == ':') { p++; if (!isdigit((unsigned char)*p)) return false;
                         while (isdigit((unsigned char)*p)) ss = ss * 10 + (*p++ - '0'); }
        if (*p) return false;
        if (hh > 167 || mm > 59 || ss > 59) return false;
        out->isfixed = true;
        out->fixed = sign * (hh * 3600 + mm * 60 + ss);
        return true;
    }
    if (strlen(name) < sizeof out->name && tz_known(name)) {
        out->isfixed = false;
        snprintf(out->name, sizeof out->name, "%s", name);
        return true;
    }
    for (int i = 0; tzabbrevs[i].ab; i++)
        if (tz_ci_eq(tzabbrevs[i].ab, name)) {
            out->isfixed = true; out->fixed = tzabbrevs[i].off;
            return true;
        }
    return false;
}

/* seconds east of UTC in effect at the given UTC instant */
int tz_offset_utc(const PgZone *z, int64 utc_usec)
{
    if (z->isfixed) return z->fixed;
    int64 secs = utc_usec >= 0 ? utc_usec / 1000000
                               : -(((-utc_usec) + 999999) / 1000000);
    int off = 0;
    tz_offset_at(z->name, secs + 946684800LL, &off, NULL, 0, NULL);
    return off;
}

/* seconds east of UTC in effect at the given local wall-clock instant */
int tz_offset_local(const PgZone *z, int64 local_usec)
{
    if (z->isfixed) return z->fixed;
    int64 secs = local_usec >= 0 ? local_usec / 1000000
                                 : -(((-local_usec) + 999999) / 1000000);
    int64 local = secs + 946684800LL, utc = local;
    if (!tz_local_to_utc(z->name, local, &utc)) return 0;
    return (int)(local - utc);
}

/* ------------------------------------------------- WAL file name parsing
 * A WAL file name is 24 hex digits: 8 timeline, 8 high log id, 8 segment.
 * The segment number PostgreSQL reports counts whole segments from the start
 * of the WAL, with the default 16MB segment size (256 segments per log). */
bool wal_split_name(const char *t, uint64 *seg, uint32 *tli)
{
    if (!t) return false;
    int n = 0;
    for (const char *p = t; *p; p++, n++)
        if (!isxdigit((unsigned char)*p)) return false;
    if (n != 24) return false;
    char a[9], b[9], cc[9];
    memcpy(a, t, 8); a[8] = 0;
    memcpy(b, t + 8, 8); b[8] = 0;
    memcpy(cc, t + 16, 8); cc[8] = 0;
    unsigned long long tl = strtoull(a, NULL, 16);
    unsigned long long hi = strtoull(b, NULL, 16);
    unsigned long long lo = strtoull(cc, NULL, 16);
    if (lo > 0xFF) return false;             /* 16MB segments: 256 per log file */
    *tli = (uint32)tl;
    *seg = hi * 256ULL + lo;
    return true;
}

/* IS JSON ... WITH UNIQUE KEYS checks every object in the document. */
bool json_keys_unique(const JsonVal *v)
{
    int k = json_kind(v);
    if (k == 5) {
        int n = json_length(v);
        for (int i = 0; i < n; i++) {
            const char *ki = json_key_at(v, i);
            for (int j = i + 1; j < n; j++) {
                const char *kj = json_key_at(v, j);
                if (ki && kj && !strcmp(ki, kj)) return false;
            }
            if (!json_keys_unique(json_val_at(v, i))) return false;
        }
    } else if (k == 4) {
        int n = json_length(v);
        for (int i = 0; i < n; i++)
            if (!json_keys_unique(json_val_at(v, i))) return false;
    }
    return true;
}

/* ------------------------------------------------------ SQL/JSON ctors
 * PostgreSQL 16's JSON_ARRAY / JSON_OBJECT / JSON_ARRAYAGG / JSON_OBJECTAGG
 * render exactly like json_build_array(), json_build_object(), json_agg() and
 * json_object_agg(), then coerce to the RETURNING type.  The spacing below is
 * part of that output, not decoration. */

/* One JSON string literal, escaped the way PostgreSQL's escape_json() does. */
void json_quote_into(Buf *b, const char *p, size_t n)
{
    buf_addc(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)p[i];
        switch (ch) {
        case '\b': buf_adds(b, "\\b"); break;
        case '\f': buf_adds(b, "\\f"); break;
        case '\n': buf_adds(b, "\\n"); break;
        case '\r': buf_adds(b, "\\r"); break;
        case '\t': buf_adds(b, "\\t"); break;
        case '"':  buf_adds(b, "\\\""); break;
        case '\\': buf_adds(b, "\\\\"); break;
        default:
            if (ch < 0x20) buf_printf(b, "\\u%04x", ch);
            else buf_addc(b, (char)ch);
        }
    }
    buf_addc(b, '"');
}

/* A value rendered the way to_json() would render it. */
void json_value_into(Session *s, Buf *b, Datum d)
{
    if (d.isnull) { buf_adds(b, "null"); return; }
    if (d.type == JSONOID || d.type == JSONBOID) {
        /* a JSON value goes in as it stands, spacing and all */
        char *t = out_func(s, d);
        buf_adds(b, t); free(t);
        return;
    }
    bool found = false;
    Datum j = call_builtin(s, "to_json", &d, 1, &found);
    if (!found) {
        char *t = out_func(s, d);
        json_quote_into(b, t, strlen(t));
        free(t);
        return;
    }
    char *t = out_func(s, j);
    buf_adds(b, t);
    free(t);
}

/* An object key: never null, always a JSON string. */
void json_key_into(Session *s, Buf *b, Datum d)
{
    if (d.isnull) ereport(s, "22004", "null value not allowed for object key");
    Datum t = coerce_to(s, d, TEXTOID, -1, 2);
    json_quote_into(b, t.v.s.p, (size_t)t.v.s.len);
}

/* Coerce a finished JSON document to the constructor's RETURNING type. */
Datum json_ctor_finish(Session *s, Expr *e, Buf *b)
{
    Oid rt = e->casttype ? e->casttype : (e->json_jsonb_default ? JSONBOID : JSONOID);
    if (rt == JSONOID || rt == JSONBOID)
        return in_func(s, rt, -1, b->data ? b->data : "", (int32)b->len);
    if (rt == TEXTOID || rt == VARCHAROID || rt == BPCHAROID || rt == NAMEOID ||
        rt == BYTEAOID || rt == UNKNOWNOID)
        return in_func(s, rt == UNKNOWNOID ? TEXTOID : rt, e->casttypmod,
                       b->data ? b->data : "", (int32)b->len);
    char *tn = format_type_str(rt, e->casttypmod);
    ereport(s, "42846", "cannot cast type json to %s", tn);
}

/* WITH UNIQUE KEYS on a constructor rejects the second copy of a key. */
static void json_ctor_unique_check(Session *s, char **keys, int nkeys, const char *k)
{
    for (int i = 0; i < nkeys; i++)
        if (!strcmp(keys[i], k))
            ereport(s, "22030", "duplicate JSON object key value: %s", k);
}

Datum eval_json_ctor(Session *s, Expr *e, Datum *args, int nargs)
{
#if PGVC_VN < 160
    ereport_pos(s, "42601", e->pos, "syntax error at or near \"(\"");
#else
    if (!e->casttype) {
        e->json_jsonb_default = false;
        for (int i = 0; i < nargs; i++)
            if (args[i].type == JSONBOID) { e->json_jsonb_default = true; break; }
    }
    Buf b; buf_init(&b);
    if (e->json_ctor == 0) {                      /* JSON_ARRAY */
        buf_addc(&b, '[');
        bool first = true;
        for (int i = 0; i < nargs; i++) {
            if (args[i].isnull && e->json_absent_on_null) continue;
            if (!first) buf_adds(&b, ", ");
            first = false;
            json_value_into(s, &b, args[i]);
        }
        buf_addc(&b, ']');
    } else if (e->json_ctor == 1) {               /* JSON_OBJECT */
        buf_addc(&b, '{');
        char **keys = NULL; int nkeys = 0;
        bool first = true;
        for (int i = 0; i + 1 < nargs; i += 2) {
            if (args[i+1].isnull && e->json_absent_on_null) continue;
            if (!first) buf_adds(&b, ", ");
            first = false;
            Buf kb; buf_init(&kb);
            json_key_into(s, &kb, args[i]);
            if (e->isjson_unique) {
                json_ctor_unique_check(s, keys, nkeys, kb.data);
                keys = xrealloc(keys, (size_t)(nkeys + 1) * sizeof(char *));
                keys[nkeys++] = xstrdup(kb.data);
            }
            buf_add(&b, kb.data, kb.len);
            buf_free(&kb);
            buf_adds(&b, " : ");
            json_value_into(s, &b, args[i+1]);
        }
        free(keys);
        buf_addc(&b, '}');
    } else if (e->json_ctor == 4) {               /* JSON(...) */
        buf_free(&b);
        Oid at = nargs ? args[0].type : UNKNOWNOID;
        if (!(at == TEXTOID || at == VARCHAROID || at == BPCHAROID || at == NAMEOID ||
              at == UNKNOWNOID || at == BYTEAOID || at == JSONOID || at == JSONBOID)) {
            char *tn = format_type_str(at, -1);
            ereport_pos(s, "42846", e->args[0]->pos, "cannot cast type %s to json", tn);
        }
        if (!nargs || args[0].isnull) return d_null(JSONOID);
        char *t = out_func(s, args[0]);
        size_t tl = strlen(t);
        Datum r = in_func(s, JSONOID, -1, t, (int32)tl);
        if (e->isjson_unique) {
            const char *err = NULL; bool dup = false;
            json_parse_keys(t, tl, false, &err, &dup);
            if (dup) ereport(s, "22030", "duplicate JSON object key value");
        }
        free(t);
        return r;
    } else if (e->json_ctor == 5) {               /* JSON_SCALAR */
        if (!nargs || args[0].isnull) { buf_free(&b); return d_null(JSONOID); }
        json_value_into(s, &b, args[0]);
        Datum r = in_func(s, JSONOID, -1, b.data, (int32)b.len);
        buf_free(&b);
        return r;
    } else if (e->json_ctor == 6) {               /* JSON_SERIALIZE */
        buf_free(&b);
        Oid rt = e->casttype ? e->casttype : TEXTOID;
        if (!(rt == TEXTOID || rt == VARCHAROID || rt == BPCHAROID || rt == NAMEOID ||
              rt == BYTEAOID || rt == JSONOID || rt == JSONBOID)) {
            char *tn = format_type_str(rt, e->casttypmod);
            err_hint(s, "Try returning a string type or bytea.");
            ereport(s, "42804", "cannot use type %s in RETURNING clause of "
                                "JSON_SERIALIZE()", tn);
        }
        if (!nargs || args[0].isnull) return d_null(rt);
        Datum j = args[0];
        if (j.type != JSONOID && j.type != JSONBOID) j = coerce_to(s, j, JSONOID, -1, 2);
        char *t = out_func(s, j);
        Datum r = in_func(s, rt, e->casttypmod, t, (int32)strlen(t));
        free(t);
        return r;
    } else {
        buf_adds(&b, "null");
    }
    Datum r = json_ctor_finish(s, e, &b);
    buf_free(&b);
    return r;
#endif
}

/* JSON_ARRAYAGG / JSON_OBJECTAGG over a group's values.  A group with no
 * input rows produces NULL, exactly like json_agg(). */
Datum eval_json_agg(Session *s, Expr *e, Datum *vals, Datum *keys, int nv)
{
#if PGVC_VN < 160
    ereport_pos(s, "42601", e->pos, "syntax error at or near \"(\"");
#else
    if (!e->casttype) {
        e->json_jsonb_default = false;
        for (int i = 0; i < nv; i++) {
            if (vals[i].type == JSONBOID || (keys && keys[i].type == JSONBOID))
                { e->json_jsonb_default = true; break; }
        }
    }
    if (nv == 0) {
        Oid rt = e->casttype ? e->casttype : JSONOID;
        return d_null(rt == UNKNOWNOID ? TEXTOID : rt);
    }
    Buf b; buf_init(&b);
    if (e->json_ctor == 2) {                      /* JSON_ARRAYAGG */
        buf_addc(&b, '[');
        bool first = true;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull && e->json_absent_on_null) continue;
            if (!first) buf_adds(&b, ", ");
            first = false;
            json_value_into(s, &b, vals[i]);
        }
        buf_addc(&b, ']');
    } else {                                      /* JSON_OBJECTAGG */
        buf_adds(&b, "{ ");
        char **seen = NULL; int nseen = 0;
        bool first = true;
        for (int i = 0; i < nv; i++) {
            if (vals[i].isnull && e->json_absent_on_null) continue;
            if (!first) buf_adds(&b, ", ");
            first = false;
            Buf kb; buf_init(&kb);
            json_key_into(s, &kb, keys[i]);
            if (e->isjson_unique) {
                json_ctor_unique_check(s, seen, nseen, kb.data);
                seen = xrealloc(seen, (size_t)(nseen + 1) * sizeof(char *));
                seen[nseen++] = xstrdup(kb.data);
            }
            buf_add(&b, kb.data, kb.len);
            buf_free(&kb);
            buf_adds(&b, " : ");
            json_value_into(s, &b, vals[i]);
        }
        free(seen);
        buf_adds(&b, " }");
    }
    Datum r = json_ctor_finish(s, e, &b);
    buf_free(&b);
    return r;
#endif
}

/* jsonb_each / json_each expansion used by FROM-clause set-returning calls */
typedef struct RowSetOpaque RowSetOpaque;
int json_each_rows(Session *s, JsonVal *v, bool txt, void *rs,
                   void (*addrow)(void *, Datum *))
{
    if (!v || json_kind(v) != 5) return 0;
    int n = json_length(v);
    for (int i = 0; i < n; i++) {
        extern const char *json_key_at(const JsonVal *v, int i);
        extern JsonVal *json_val_at(const JsonVal *v, int i);
        const char *k = json_key_at(v, i);
        JsonVal *val = json_val_at(v, i);
        Datum *row = xmalloc(2 * sizeof(Datum));
        row[0] = d_text(TEXTOID, k ? k : "");
        if (txt) {
            if (json_kind(val) == 3) row[1] = d_text(TEXTOID, json_str_value(val));
            else if (json_kind(val) == 0) row[1] = d_null(TEXTOID);
            else { char *t = json_to_text(val, true); row[1] = d_text(TEXTOID, t); free(t); }
        } else {
            Datum d; memset(&d, 0, sizeof d);
            d.type = JSONBOID; d.v.jb = val;
            row[1] = d;
        }
        addrow(rs, row);
    }
    (void)s;
    return n;
}

/* ----------------------------------------------------- binary encoding */
static void put_be32(Buf *b, uint32 v)
{
    buf_addc(b, (char)(v >> 24)); buf_addc(b, (char)(v >> 16));
    buf_addc(b, (char)(v >> 8));  buf_addc(b, (char)v);
}
static void put_be64(Buf *b, uint64 v) { put_be32(b, (uint32)(v >> 32)); put_be32(b, (uint32)v); }
static uint32 get_be32(const unsigned char *p)
{ return ((uint32)p[0]<<24)|((uint32)p[1]<<16)|((uint32)p[2]<<8)|p[3]; }
static uint64 get_be64(const unsigned char *p)
{ return ((uint64)get_be32(p) << 32) | get_be32(p + 4); }

void datum_to_binary(Session *s, Datum d, Buf *b)
{
    switch (d.type) {
    case BOOLOID: buf_addc(b, d.v.i ? 1 : 0); return;
    case INT2OID: buf_addc(b, (char)(d.v.i >> 8)); buf_addc(b, (char)d.v.i); return;
    case INT4OID: case OIDOID: case REGCLASSOID: case REGTYPEOID: case REGPROCOID:
        put_be32(b, (uint32)d.v.i); return;
    case INT8OID: put_be64(b, (uint64)d.v.i); return;
    case DATEOID: put_be32(b, (uint32)(int32)d.v.i); return;
    case TIMEOID: case TIMESTAMPOID: case TIMESTAMPTZOID: put_be64(b, (uint64)d.v.i); return;
    case FLOAT4OID: { float f = (float)d.v.f; uint32 u; memcpy(&u, &f, 4); put_be32(b, u); return; }
    case FLOAT8OID: { double f = d.v.f; uint64 u; memcpy(&u, &f, 8); put_be64(b, u); return; }
    case INTERVALOID: put_be64(b, (uint64)d.v.iv.usec); put_be32(b, (uint32)d.v.iv.day);
                      put_be32(b, (uint32)d.v.iv.month); return;
    case BYTEAOID: buf_add(b, d.v.s.p, (size_t)d.v.s.len); return;
    case UUIDOID: {
        for (int i = 0, o = 0; i < d.v.s.len && o < 16; i++) {
            if (d.v.s.p[i] == '-') continue;
            int hi = d.v.s.p[i], lo = d.v.s.p[i+1];
            int hv = isdigit((unsigned char)hi) ? hi - '0' : tolower((unsigned char)hi) - 'a' + 10;
            int lv = isdigit((unsigned char)lo) ? lo - '0' : tolower((unsigned char)lo) - 'a' + 10;
            buf_addc(b, (char)((hv << 4) | lv));
            i++; o++;
        }
        return;
    }
    case JSONBOID: { buf_addc(b, 1); char *t = json_to_text(d.v.jb, true); buf_adds(b, t); free(t); return; }
    case NUMERICOID: {
        /* PostgreSQL's binary numeric: ndigits, weight, sign, dscale, digits */
        char *t = num_to_str(d.v.num);
        bool neg = (*t == '-');
        const char *p = neg ? t + 1 : t;
        const char *dot = strchr(p, '.');
        int intlen = dot ? (int)(dot - p) : (int)strlen(p);
        int fraclen = dot ? (int)strlen(dot + 1) : 0;
        if (!strcmp(p, "NaN")) { put_be32(b, 0); buf_addc(b,0); buf_addc(b,0);
                                 buf_addc(b, (char)0xC0); buf_addc(b, 0); buf_addc(b,0); buf_addc(b,0);
                                 free(t); return; }
        int pad = (4 - intlen % 4) % 4;
        int total = intlen + pad + fraclen + (4 - fraclen % 4) % 4;
        char digits[512];
        memset(digits, '0', sizeof digits);
        memcpy(digits + pad, p, (size_t)intlen);
        if (dot) memcpy(digits + pad + intlen, dot + 1, (size_t)fraclen);
        int ndig = total / 4;
        int weight = (intlen + pad) / 4 - 1;
        Buf tmp; buf_init(&tmp);
        int emitted = 0;
        for (int i = 0; i < ndig; i++) {
            int v = 0;
            for (int k = 0; k < 4; k++) v = v * 10 + (digits[i*4+k] - '0');
            buf_addc(&tmp, (char)(v >> 8)); buf_addc(&tmp, (char)v);
            emitted++;
        }
        put_be32(b, 0);
        b->len -= 4;
        buf_addc(b, (char)(emitted >> 8)); buf_addc(b, (char)emitted);
        buf_addc(b, (char)(weight >> 8)); buf_addc(b, (char)weight);
        buf_addc(b, (char)(neg ? 0x40 : 0)); buf_addc(b, 0);
        buf_addc(b, (char)(fraclen >> 8)); buf_addc(b, (char)fraclen);
        buf_add(b, tmp.data, tmp.len);
        buf_free(&tmp);
        free(t);
        return;
    }
    default: {
        if (type_is_array(d.type)) {
            ArrayVal *a = d.v.arr;
            put_be32(b, (uint32)a->ndim);
            put_be32(b, 0);
            put_be32(b, a->elemtype);
            for (int i = 0; i < a->ndim; i++) { put_be32(b, (uint32)a->dims[i]); put_be32(b, (uint32)a->lbound[i]); }
            for (int i = 0; i < a->nelem; i++) {
                if (a->elems[i].isnull) { put_be32(b, 0xFFFFFFFFu); continue; }
                Buf eb; buf_init(&eb);
                datum_to_binary(s, a->elems[i], &eb);
                put_be32(b, (uint32)eb.len);
                buf_add(b, eb.data, eb.len);
                buf_free(&eb);
            }
            return;
        }
        char *t = out_func(s, d);
        buf_adds(b, t);
        free(t);
        return;
    }
    }
}

Datum binary_to_datum(Session *s, Oid type, const unsigned char *p, int len)
{
    switch (type) {
    case BOOLOID: return d_bool(len && p[0]);
    case INT2OID: return d_int(INT2OID, (int16)((p[0] << 8) | p[1]));
    case INT4OID: case OIDOID: return d_int(type, (int32)get_be32(p));
    case INT8OID: return d_int(INT8OID, (int64)get_be64(p));
    case DATEOID: return d_int(DATEOID, (int32)get_be32(p));
    case TIMEOID: case TIMESTAMPOID: case TIMESTAMPTZOID:
        return d_int(type, (int64)get_be64(p));
    case FLOAT4OID: { uint32 u = get_be32(p); float f; memcpy(&f, &u, 4); return d_float(FLOAT4OID, f); }
    case FLOAT8OID: { uint64 u = get_be64(p); double f; memcpy(&f, &u, 8); return d_float(FLOAT8OID, f); }
    case INTERVALOID: {
        Datum d = d_int(INTERVALOID, 0);
        d.v.iv.usec = (int64)get_be64(p);
        d.v.iv.day = (int32)get_be32(p + 8);
        d.v.iv.month = (int32)get_be32(p + 12);
        return d;
    }
    case BYTEAOID: return d_textn(BYTEAOID, (const char *)p, len);
    case NUMERICOID: {
        int nd = (int)((p[0] << 8) | p[1]);
        int16 weight = (int16)((p[2] << 8) | p[3]);
        uint16 sign = (uint16)((p[4] << 8) | p[5]);
        int dscale = (int)((p[6] << 8) | p[7]);
        if (sign == 0xC000) { bool ok; return d_num(num_from_str("NaN", &ok)); }
        Buf b; buf_init(&b);
        if (sign == 0x4000) buf_addc(&b, '-');
        if (weight < 0) buf_addc(&b, '0');
        for (int i = 0; i < nd; i++) {
            int v = (int)((p[8 + i*2] << 8) | p[9 + i*2]);
            char t[8];
            if (i == 0) snprintf(t, sizeof t, "%d", v);
            else snprintf(t, sizeof t, "%04d", v);
            buf_adds(&b, t);
            if (i == weight && i + 1 < nd) buf_addc(&b, '.');
        }
        if (weight >= nd) for (int i = nd; i <= weight; i++) buf_adds(&b, "0000");
        char *txt = buf_steal(&b);
        if (dscale > 0 && !strchr(txt, '.')) {
            Buf c; buf_init(&c); buf_adds(&c, txt); buf_addc(&c, '.');
            for (int i = 0; i < dscale; i++) buf_addc(&c, '0');
            free(txt); txt = buf_steal(&c);
        }
        bool ok;
        Numeric *n = num_from_str(txt, &ok);
        free(txt);
        return d_num(n);
    }
    case JSONBOID: return in_func(s, JSONBOID, -1, (const char *)p + 1, len - 1);
    case UUIDOID: {
        char hex[33];
        for (int i = 0; i < 16 && i < len; i++) snprintf(hex + i*2, 3, "%02x", p[i]);
        return in_func(s, UUIDOID, -1, hex, 32);
    }
    default: return in_func(s, type, -1, (const char *)p, len);
    }
}
