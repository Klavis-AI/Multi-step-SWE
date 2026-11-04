# -*- coding: us-ascii -*-
# frozen_string_literal: false
class MyInteger
    def initialize(n); @num = n; end
    def to_int; @num; end
    def <=>(n); @num <=> n.to_int; end
    def <=(n); @num <= n.to_int; end
    def +(n); MyInteger.new(@num + n.to_int); end
  end
class InstanceExec
    INSTANCE_EXEC = 123
  end
def assert_not_initialize_copy
    a = yield
    b = yield
    assert_nothing_raised("copy") {a.instance_eval {initialize_copy(b)}}
    c = a.dup.freeze
    assert_raise(FrozenError, "frozen") {c.instance_eval {initialize_copy(b)}}
    d = a.dup
    [a, b, c, d]
  end
@verbose = $VERBOSE
str = false.to_s
    p(str)
    p(str.send(:frozen?))
    p(false.to_s.equal?(str))