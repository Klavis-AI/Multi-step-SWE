def m0() end
def m1(a) end
def m2(a, b) end
def mo1(a = nil, &b) end
def mo2(a, b = nil) end
def mo3(*a) end
def mo4(a, *b, &c) end
class Base
    def foo() :base end
  end
class Derived < Base
    def foo() :derived end
  end
class T
    def initialize; end
    def normal_method; end
  end
module M
    def func; end
    module_function :func
    def meth; end
  end
@verbose = $VERBOSE
    $VERBOSE = nil
c = Class.new
    c.class_eval { attr_accessor :foo }
    o = c.new
    o.method(:foo=).call(42)
    p(o.foo)
    begin; o.method(:foo=).call(1, 2, 3)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; o.method(:foo).call(1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end