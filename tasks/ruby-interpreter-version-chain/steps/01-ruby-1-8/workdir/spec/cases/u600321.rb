# frozen_string_literal: false
def enum_test obj
    obj.map{|e|
      e
    }.sort
  end
def check_consistency_for_combinatorics(method)
    [ [], [:a, :b, :c, :d, :e] ].product([-2, 0, 2, 5, 6]) do |array, arg|
      assert_equal array.send(method, arg).to_a.size, array.send(method, arg).size,
        "inconsistent size for #{array}.#{method}(#{arg})"
    end
  end
@obj = Object.new
    class << @obj
      include Enumerable
      def foo(*a)
        a.each {|x| yield x }
      end
    end
    @sized = @obj.clone
    def @sized.size
      42
    end
e = @obj.to_enum(:foo, 1, 2, 3)
    p(e.next)
    p(e.next)
    e.rewind
    p(e.next)
    p(e.next)
    p(e.next)
    begin; e.next
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end