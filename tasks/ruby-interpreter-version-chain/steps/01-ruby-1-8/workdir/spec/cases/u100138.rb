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
    i = 0
    loop{
      p(e.next)
      i += 1
    }