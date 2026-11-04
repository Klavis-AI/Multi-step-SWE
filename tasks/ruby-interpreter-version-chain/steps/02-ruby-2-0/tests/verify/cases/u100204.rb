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
p(Derived.new.foo)
    um = Derived.new.method(:foo).unbind
    p(um.class)
    Derived.class_eval do
      def foo() :changed end
    end
    p(Derived.new.foo)
    p(um.bind(Derived.new).call)
    begin; um.bind(Base.new)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end