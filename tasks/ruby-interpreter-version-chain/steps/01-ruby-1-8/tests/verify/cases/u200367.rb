class DummyNumeric < Numeric
    def to_int
      1
    end
  end
def assert_eql(a, b, mess)
    assert a.eql?(b), "expected #{a} & #{b} to be eql? #{mess}"
  end
@verbose = $VERBOSE
    $VERBOSE = nil
p(1 != nil)

    p(1 <=> 1)
    p(1 <=> 4294967296)
    p(1 <=> 1.0)
    p(1 <=> nil)

    p(1.send(:>, 0))
    p(!(1.send(:>, 1)))
    p(!(1.send(:>, 2)))
    p(!(1.send(:>, 4294967296)))
    p(1.send(:>, 0.0))
    begin; 1.send(:>, nil)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(1.send(:>=, 0))
    p(1.send(:>=, 1))
    p(!(1.send(:>=, 2)))
    p(!(1.send(:>=, 4294967296)))
    p(1.send(:>=, 0.0))
    begin; 1.send(:>=, nil)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(!(1.send(:<, 0)))
    p(!(1.send(:<, 1)))
    p(1.send(:<, 2))
    p(1.send(:<, 4294967296))
    p(!(1.send(:<, 0.0)))
    begin; 1.send(:<, nil)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(!(1.send(:<=, 0)))
    p(1.send(:<=, 1))
    p(1.send(:<=, 2))
    p(1.send(:<=, 4294967296))
    p(!(1.send(:<=, 0.0)))
    begin; 1.send(:<=, nil)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end