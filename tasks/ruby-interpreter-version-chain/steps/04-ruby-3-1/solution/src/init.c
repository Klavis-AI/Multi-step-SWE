#include "internal.h"

extern void rb_eval_init(void);
extern VALUE rb_obj_alloc_of(RClass *);
extern RClass *rb_class_boot(RClass *);
extern void rb_const_set(RClass *, ID, VALUE);
extern void rb_define_private_method(RClass *, const char *, VALUE (*)(), int);

static RBasic nil_s, true_s, false_s, undef_s;

static RClass *boot_defclass(const char *name, RClass *super){
  RClass *c = rb_class_boot(super);
  c->name = rb_intern(name);
  if(rb_cObject) rb_const_set(rb_cObject, c->name, (VALUE)c);
  return c;
}

void rb_init(void){
  /* --- bootstrap the core three --- */
  rb_cBasicObject = boot_defclass("BasicObject", NULL);
  rb_cObject      = boot_defclass("Object", rb_cBasicObject);
  rb_cModule      = boot_defclass("Module", rb_cObject);
  rb_cClass       = boot_defclass("Class", rb_cModule);
  rb_const_set(rb_cObject, ID_CACHED("BasicObject"), (VALUE)rb_cBasicObject);
  rb_const_set(rb_cObject, ID_CACHED("Object"), (VALUE)rb_cObject);
  rb_const_set(rb_cObject, ID_CACHED("Module"), (VALUE)rb_cModule);
  rb_const_set(rb_cObject, ID_CACHED("Class"), (VALUE)rb_cClass);
  rb_cBasicObject->basic.klass = rb_cClass;
  rb_cObject->basic.klass = rb_cClass;
  rb_cModule->basic.klass = rb_cClass;
  rb_cClass->basic.klass = rb_cClass;

  /* metaclasses */
  RClass *sBasic = rb_singleton_class((VALUE)rb_cBasicObject);
  sBasic->super = rb_cClass;
  rb_singleton_class((VALUE)rb_cObject);
  rb_singleton_class((VALUE)rb_cModule);
  rb_singleton_class((VALUE)rb_cClass);

  rb_cKernel = rb_define_module("Kernel");
  rb_include_module(rb_cObject, rb_cKernel);

  /* --- singletons --- */
  rb_cNilClass = rb_define_class("NilClass", rb_cObject);
  rb_cTrueClass = rb_define_class("TrueClass", rb_cObject);
  rb_cFalseClass = rb_define_class("FalseClass", rb_cObject);
  nil_s.type = T_NIL;  nil_s.klass = rb_cNilClass;  nil_s.frozen = 1;
  true_s.type = T_TRUE; true_s.klass = rb_cTrueClass; true_s.frozen = 1;
  false_s.type = T_FALSE; false_s.klass = rb_cFalseClass; false_s.frozen = 1;
  undef_s.type = T_NIL; undef_s.klass = rb_cNilClass; undef_s.frozen = 1;
  Qnil = (VALUE)&nil_s; Qtrue = (VALUE)&true_s; Qfalse = (VALUE)&false_s; Qundef = (VALUE)&undef_s;
  rb_errinfo = Qnil;

  rb_cComparable = rb_define_module("Comparable");
  rb_cEnumerable = rb_define_module("Enumerable");

  rb_cNumeric = rb_define_class("Numeric", rb_cObject);
  rb_include_module(rb_cNumeric, rb_cComparable);
  rb_cInteger = rb_define_class("Integer", rb_cNumeric);
  rb_cFloat   = rb_define_class("Float", rb_cNumeric);
  rb_cString  = rb_define_class("String", rb_cObject);
  rb_include_module(rb_cString, rb_cComparable);
  rb_cSymbol  = rb_define_class("Symbol", rb_cObject);
  rb_include_module(rb_cSymbol, rb_cComparable);
  rb_cArray   = rb_define_class("Array", rb_cObject);
  rb_include_module(rb_cArray, rb_cEnumerable);
  rb_cHash    = rb_define_class("Hash", rb_cObject);
  rb_include_module(rb_cHash, rb_cEnumerable);
  rb_cRange   = rb_define_class("Range", rb_cObject);
  rb_include_module(rb_cRange, rb_cEnumerable);
  rb_cProc    = rb_define_class("Proc", rb_cObject);
  rb_cMethod  = rb_define_class("Method", rb_cObject);
  rb_cUnboundMethod = rb_define_class("UnboundMethod", rb_cObject);
  rb_cRegexp  = rb_define_class("Regexp", rb_cObject);
  rb_cMatchData = rb_define_class("MatchData", rb_cObject);
  rb_cStruct  = rb_define_class("Struct", rb_cObject);
  rb_include_module(rb_cStruct, rb_cEnumerable);
  rb_cEnumerator = rb_define_class("Enumerator", rb_cObject);
  rb_include_module(rb_cEnumerator, rb_cEnumerable);
  rb_cEncoding = rb_define_class("Encoding", rb_cObject);
  rb_cBinding = rb_define_class("Binding", rb_cObject);

  /* exceptions */
  rb_cException = rb_define_class("Exception", rb_cObject);
  rb_eScriptError = rb_define_class("ScriptError", rb_cException);
  rb_eSyntaxError = rb_define_class("SyntaxError", rb_eScriptError);
  rb_eNotImpError = rb_define_class("NotImplementedError", rb_eScriptError);
  rb_eSystemExit = rb_define_class("SystemExit", rb_cException);
  rb_eNoMemoryError = rb_define_class("NoMemoryError", rb_cException);
  rb_eSignalException = rb_define_class("SignalException", rb_cException);
  rb_eInterrupt = rb_define_class("Interrupt", rb_eSignalException);
  rb_eSystemStackError = rb_define_class("SystemStackError", rb_cException);
  rb_eStandardError = rb_define_class("StandardError", rb_cException);
  rb_eRuntimeError = rb_define_class("RuntimeError", rb_eStandardError);
  rb_eFrozenError = rb_define_class("FrozenError",
#if RUBY_TARGET >= 27
    rb_eRuntimeError
#else
    rb_eRuntimeError
#endif
  );
  rb_eArgError = rb_define_class("ArgumentError", rb_eStandardError);
  rb_eTypeError = rb_define_class("TypeError", rb_eStandardError);
  rb_eNameError = rb_define_class("NameError", rb_eStandardError);
  rb_eNoMethodError = rb_define_class("NoMethodError", rb_eNameError);
  rb_eIndexError = rb_define_class("IndexError", rb_eStandardError);
  rb_eKeyError = rb_define_class("KeyError", rb_eIndexError);
  rb_eRangeError = rb_define_class("RangeError", rb_eStandardError);
  rb_eFloatDomainError = rb_define_class("FloatDomainError", rb_eRangeError);
  rb_eZeroDiv = rb_define_class("ZeroDivisionError", rb_eStandardError);
  rb_eStopIteration = rb_define_class("StopIteration", rb_eIndexError);
  rb_eLocalJumpError = rb_define_class("LocalJumpError", rb_eStandardError);
  rb_eIOError = rb_define_class("IOError", rb_eStandardError);
  rb_eEncodingError = rb_define_class("EncodingError", rb_eStandardError);
  rb_eEncCompatError = rb_define_class_under(rb_cEncoding, "CompatibilityError", rb_eEncodingError);
  rb_eSecurityError = rb_define_class("SecurityError", rb_cException);
  rb_eFiberError = rb_define_class("FiberError", rb_eStandardError);
  rb_eUncaughtThrow = rb_define_class("UncaughtThrowError", rb_eArgError);
  rb_eConvError = rb_define_class_under(rb_cEncoding, "ConverterNotFoundError", rb_eEncodingError);
#if RUBY_TARGET >= 27
  rb_eNoMatchingPattern = rb_define_class("NoMatchingPatternError", rb_eStandardError);
#endif
#if RUBY_TARGET >= 31
  rb_eNoMatchingPatternKey = rb_define_class("NoMatchingPatternKeyError", rb_eNoMatchingPattern);
  rb_cRactor = rb_define_class("Ractor", rb_cObject);
  rb_eRactorError = rb_define_class_under(rb_cRactor, "Error", rb_eStandardError);
  rb_eRactorIsolationError = rb_define_class_under(rb_cRactor, "IsolationError", rb_eRactorError);
#endif
#if RUBY_TARGET >= 33
  rb_cData = rb_define_class("Data", rb_cObject);
#endif
  rb_cFiber = rb_define_class("Fiber", rb_cObject);
  rb_cLazy = rb_define_class_under(rb_cEnumerator, "Lazy", rb_cEnumerator);

  rb_eval_init();
  rb_init_object();
  rb_init_string();
  rb_init_array();
  rb_init_hash();
  rb_init_numeric();
  rb_init_enum();
  rb_init_enumerator();
  rb_init_regexp();
  rb_init_struct();
  rb_init_misc();
  extern int rb_init_done;
  rb_init_done = 1;
}
