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
a = [[1,2,3], [2,3,4], [3,4,5], [4,5,6], [5,6,7], [6,7,8], [7,8,9], [8,9,10]]
    p((1..10).each_cons(3).to_a)