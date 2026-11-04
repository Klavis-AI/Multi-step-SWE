def enum_test obj
    i = 0
    obj.map{|e|
      e
    }.sort
  end
@obj = Object.new
    class << @obj
      include Enumerable
      def foo(*a)
        a.each {|x| yield x }
      end
    end
p(@obj.to_enum(:foo, 1, 2, 3).with_index.to_a)