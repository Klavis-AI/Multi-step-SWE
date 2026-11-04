# frozen_string_literal: false
def assert_num_equal_type(ary1, ary2, message=nil)
    assert_equal(ary1.length, ary2.length, message)
    ary1.zip(ary2) do |e1, e2|
      assert_equal(e1.class, e2.class, message)
      if e1.is_a? Complex
        assert_equal(e1.real, e2.real, message)
        assert_equal(e1.imag, e2.imag, message)
      else
        assert_equal(e1, e2, message)
      end
    end
  end
obj = [0, 1]
    seq = 1.step(10, 2)
    ret = seq.each_with_object(obj) do |i, memo|
      memo[0] += i
      memo[1] *= i
    end
    p(ret.equal?(obj))
    p(ret)

    obj = [0, 1]
    seq = 10.step(1, -2)
    ret = seq.each_with_object(obj) do |i, memo|
      memo[0] += i
      memo[1] *= i
    end
    p(ret.equal?(obj))
    p(ret)