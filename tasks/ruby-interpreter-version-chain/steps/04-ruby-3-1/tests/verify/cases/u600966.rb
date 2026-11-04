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
bug5731 = '[ruby-dev:44961]'

    c = Class.new do
      def method_missing(meth, *args) yield(meth, *args) end
    end
    a = c.new
    result = nil
    begin; a.foo {|x| result = x}
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(result)
    result = nil
    e = a.enum_for(:foo)
    begin; e.each {|x| result = x}
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(result)

    c = Class.new do
      def respond_to_missing?(id, priv)
        true
      end
      def method_missing(id, *args, &block)
        return block.call(:foo, *args)
      end
    end
    foo = c.new

    result = nil
    begin; foo.foobar {|x| result = x}
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(result)
    result = nil
    begin; foo.enum_for(:foobar).each {|x| result = x}
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(result)

    result = nil
    foobar = foo.method(:foobar)
    foobar.call {|x| result = x}
    p(result)

    result = nil
    foobar = foo.method(:foobar)
    foobar.enum_for(:call).each {|x| result = x}
    p(result)