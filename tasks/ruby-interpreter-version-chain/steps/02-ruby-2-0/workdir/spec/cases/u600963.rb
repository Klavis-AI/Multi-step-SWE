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
c = Class.new do
      def respond_to_missing?(id, priv)
        if id == :foobar
          true
        else
          false
        end
      end
      def method_missing(id, *args)
        if id == :foobar
          return [:foo, *args]
        else
          super
        end
      end
    end

    foo = c.new
    p(foo.foobar);
    p(foo.foobar(1));
    p(foo.foobar(1, 2, 3, 4, 5));
    p(foo.respond_to?(:foobar))
    assert_not_respond_to(foo, :foobarbaz)
    begin; foo.foobarbaz
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    foobar = foo.method(:foobar)
    p(foobar.arity);
    p(foobar.call);
    p(foobar.call(1));
    p(foobar.call(1, 2, 3, 4, 5));
    p(foo.method(:foobar))
    p(c.new.method(:foobar))

    c = Class.new(c)
    p(c.method_defined?(:foobar))
    begin; c.instance_method(:foobar)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    m = Module.new
    p(m.method_defined?(:foobar))
    begin; m.instance_method(:foobar)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end