--TEST--
Engine identity (identity-engine) for this release
--FILE--
<?php
var_dump(class_exists("Error"), class_exists("Throwable"), class_exists("ValueError"),
         class_exists("WeakMap"), class_exists("Fiber"), class_exists("UnitEnum"),
         class_exists("ArgumentCountError"), class_exists("JsonException"),
         class_exists("Stringable"), class_exists("WeakReference"),
         class_exists("UnhandledMatchError"));
var_dump(function_exists("intdiv"), function_exists("str_contains"),
         function_exists("array_key_first"), function_exists("array_is_list"),
         function_exists("json_validate"), function_exists("array_find"),
         function_exists("each"), function_exists("get_debug_type"),
         function_exists("is_iterable"), function_exists("spl_object_id"),
         function_exists("mb_str_split"), function_exists("fdiv"));
--EXPECT--
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)
bool(true)
bool(false)
bool(true)
bool(true)
bool(false)
bool(false)
