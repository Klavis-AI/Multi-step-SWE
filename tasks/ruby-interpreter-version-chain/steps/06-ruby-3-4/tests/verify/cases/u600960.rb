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
o1 = MyInteger.new(1)
    o9 = MyInteger.new(9)
    n = 0
    Range.new(o1, o9).step(2) {|x| n += x.to_int }
    p(n)