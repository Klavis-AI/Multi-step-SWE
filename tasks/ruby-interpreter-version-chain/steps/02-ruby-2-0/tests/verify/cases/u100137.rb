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
e = 3.times
    3.times{|i|
      p(e.next)
    }
    begin; e.next
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end