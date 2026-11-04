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
o = Object.new
    def o.foo; end
    p(o.method(:foo).hash.class)