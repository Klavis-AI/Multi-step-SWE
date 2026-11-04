def assert_arity(n)
    meta = class << self; self; end
    meta.class_eval {define_method(:foo, Proc.new)}
    assert_equal(n, method(:foo).arity)
  end
def m(x)
    lambda { x }
  end
p(Proc.new{|&b| b.call(10)}.call {|x| x})
    p(Proc.new{|a,&b| b.call(a)}.call(12) {|x| x})