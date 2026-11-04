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
s = Struct.new(:foo, :bar, :baz).new(1, 2)
    %i[each each_pair select].each do |method|
      p(s.send(method).size)
    end