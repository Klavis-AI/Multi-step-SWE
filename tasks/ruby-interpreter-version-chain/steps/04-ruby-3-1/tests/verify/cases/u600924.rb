# frozen_string_literal: false
def assert_step(expected, (from, *args), inf: false)
    kw = args.last.is_a?(Hash) ? args.pop : {}
    enum = from.step(*args, **kw)
    size = enum.size
    xsize = expected.size

    if inf
      assert_send [size, :infinite?], "step size: +infinity"
      assert_send [size, :>, 0], "step size: +infinity"

      a = []
      from.step(*args, **kw) { |x| a << x; break if a.size == xsize }
      assert_equal expected, a, "step"

      a = []
      enum.each { |x| a << x; break if a.size == xsize }
      assert_equal expected, a, "step enumerator"
    else
      assert_equal expected.size, size, "step size"

      a = []
      from.step(*args, **kw) { |x| a << x }
      assert_equal expected, a, "step"

      a = []
      enum.each { |x| a << x }
      assert_equal expected, a, "step enumerator"
    end
  end
bug17183 = "[ruby-core:100090]"
    f = 291.4
    31.times do |i|
      p(f.floor(i+1))
      p(f.ceil(i+1))
    end