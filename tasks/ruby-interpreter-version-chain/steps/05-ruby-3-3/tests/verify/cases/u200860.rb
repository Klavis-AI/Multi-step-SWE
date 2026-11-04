def assert_arity(n)
    meta = class << self; self; end
    meta.class_eval {define_method(:foo, Proc.new)}
    assert_equal(n, method(:foo).arity)
  end
def m(x)
    lambda { x }
  end
def m2
    "OK"
  end
def block
    method(:m2).to_proc
  end
def m1(var)
    var
  end
def m_block_given?
    m1(block_given?)
  end
def pm0() end
def pm1(a) end
def pm2(a, b) end
def pmo1(a = :a, &b) end
def pmo2(a, b = :b) end
def pmo3(*a) end
def pmo4(a, *b, &c) end
def pmo5(a, *b, c) end
def pmo6(a, *b, c, &d) end
def pmo7(a, b = :b, *c, d, &e) end
def pma1((a), &b) a; end
def source_location_test a=1,
    b=2
  end
def block_source_location_test(*args, &block)
    block.source_location
  end
@verbose = $VERBOSE
    $VERBOSE = nil
bug8341 = '[Bug #8341]'
    o = Object.new
    def o.m1(top=true)
      if top
        [block_given?, @m.call(false)]
      else
        block_given?
      end
    end
    m = o.method(:m1).to_proc
    o.instance_variable_set(:@m, m)
    p(m.call {})
    p(m.call)