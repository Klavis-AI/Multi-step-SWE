# frozen_string_literal: false
def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
def return_in_current(val)
    1.tap(&->(*) {return 0})
    val
  end
def yield_block
    yield
  end
def return_in_callee(val)
    yield_block(&->(*) {return 0})
    val
  end
def break_in_current(val)
    1.tap(&->(*) {break 0})
    val
  end
def break_in_callee(val)
    yield_block(&->(*) {break 0})
    val
  end
p(Module.new { extend self
      def m1(&b) b.call end; def m2(); m1(&-> { next 42 }) end }.m2)
    p(Module.new { extend self
      def m1(&b) b end; def m2(); m1(&-> { next 42 }).call end }.m2)
    p(Module.new { extend self
      def m1(&b) b end; def m2(); m1(&-> { next 42 }) end }.m2.call)