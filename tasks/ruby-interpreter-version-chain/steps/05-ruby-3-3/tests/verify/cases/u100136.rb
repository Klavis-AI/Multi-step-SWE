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
p(enum_test(3.times))
    p(enum_test(["z", "y", "x"].each))
    p(enum_test({"y"=>2, "x"=>1}))