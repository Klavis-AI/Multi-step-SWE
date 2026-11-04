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
c1 = Class.new
    c1.class_eval { def foo; :foo; end }
    c2 = Class.new(c1)
    c2.class_eval { private :foo }
    o = c2.new
    o.extend(Module.new)
    begin; o.method(:bar)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(o.method(:foo).call)