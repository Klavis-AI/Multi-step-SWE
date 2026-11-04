# -*- coding: us-ascii -*-
def m0() end
def m1(a) end
def m2(a, b) end
def mo1(a = nil, &b) end
def mo2(a, b = nil) end
def mo3(*a) end
def mo4(a, *b, &c) end
def mo5(a, *b, c) end
def mo6(a, *b, c, &d) end
def mo7(a, b = nil, *c, d, &e) end
def ma1((a), &b) end
class Base
    def foo() :base end
  end
class Derived < Base
    def foo() :derived end
  end
class T
    def initialize; end
    def initialize_copy(*) super end
    def initialize_clone(*) super end
    def initialize_dup(*) super end
    def respond_to_missing?(*) super end
    def normal_method; end
  end
module M
    def func; end
    module_function :func
    def meth; :meth end
  end
def mv1() end
def mv2() end
def mv3() end
class Visibility
    def mv1() end
    def mv2() end
    private :mv2
    def mv3() end
    protected :mv3
  end
@verbose = $VERBOSE
    $VERBOSE = nil
p(method(:m0).arity)
    p(method(:m1).arity)
    p(method(:m2).arity)
    p(method(:mo1).arity)
    p(method(:mo2).arity)
    p(method(:mo3).arity)
    p(method(:mo4).arity)
    p(method(:mo5).arity)
    p(method(:mo6).arity)