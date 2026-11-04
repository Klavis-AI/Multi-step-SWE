@obj = Object.new
    class << @obj
      include Enumerable
      def each
        yield 1
        yield 2
        yield 3
        yield 1
        yield 2
      end
    end
    @verbose = $VERBOSE
    $VERBOSE = nil
p(@obj.max)
    p(@obj.max {|a,b| b <=> a })
    a = %w(albatross dog horse)
    p(a.max)
    p(a.max {|a,b| a.length <=> b.length })
    p([3,2,1].max{|a,b| b <=> a })