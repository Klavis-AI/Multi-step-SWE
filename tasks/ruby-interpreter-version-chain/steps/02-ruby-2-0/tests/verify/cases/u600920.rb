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
a = Class.new(Numeric) do
      def to_i; :ok; end
    end.new

    p(a.to_int)