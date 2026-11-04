# -*- coding: us-ascii -*-
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
    assert_raise(RuntimeError, "frozen") {c.instance_eval {initialize_copy(b)}}
    d = a.dup.trust
    assert_raise(SecurityError, "untrust") do
      proc {
        $SAFE = 4
        d.instance_eval {initialize_copy(b)}
      }.call
    end
    [a, b, c, d]
  end
@verbose = $VERBOSE
    $VERBOSE = nil
o = Object.new
    o.freeze
    begin; o.taint
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    o = Object.new
    o.taint
    o.freeze
    begin; o.untaint
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end