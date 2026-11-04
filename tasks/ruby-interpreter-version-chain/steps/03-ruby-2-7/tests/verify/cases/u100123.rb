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
p(@obj.minmax)
    p(@obj.minmax {|a,b| b <=> a })
    a = %w(albatross dog horse)
    p(a.minmax)
    p(a.minmax {|a,b| a.length <=> b.length })
    p([2,3,1].minmax)
    p([2,3,1].minmax {|a,b| b <=> a })