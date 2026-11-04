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
h = {a: 1, b: 2, c: 3}
    methods = %i[delete_if reject reject! select select! filter filter! keep_if each each_key each_pair]
    enums = methods.map {|method| h.send(method)}
    s = enums.group_by(&:size)
    p(s.keys)
    h[:d] = 4
    s = enums.group_by(&:size)
    p(s.keys)