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
p(defined?(mv1))
    p(defined?(mv2))
    p(defined?(mv3))

    p(defined?(self.mv1))
    p(defined?(self.mv2))
    p(defined?(self.mv3))

    p(respond_to?(:mv1))
    p(respond_to?(:mv2))
    p(respond_to?(:mv3))

    p(respond_to?(:mv1, true))
    p(respond_to?(:mv2, true))
    p(respond_to?(:mv3, true))

    begin; mv1
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; mv2
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; mv3
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    begin; self.mv1
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; self.mv2
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; self.mv3
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    v = Visibility.new

    p(defined?(v.mv1))
    p(defined?(v.mv2))
    p(defined?(v.mv3))

    p(v.respond_to?(:mv1))
    p(v.respond_to?(:mv2))
    p(v.respond_to?(:mv3))

    p(v.respond_to?(:mv1, true))
    p(v.respond_to?(:mv2, true))
    p(v.respond_to?(:mv3, true))

    begin; v.mv1
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; v.mv2
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; v.mv3
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    begin; v.__send__(:mv1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; v.__send__(:mv2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; v.__send__(:mv3)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    begin; v.instance_eval { mv1 }
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; v.instance_eval { mv2 }
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; v.instance_eval { mv3 }
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end