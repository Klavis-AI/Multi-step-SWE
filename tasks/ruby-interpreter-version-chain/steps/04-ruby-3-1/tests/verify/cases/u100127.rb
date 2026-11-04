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
p(@obj.member?(1))
    p(!(@obj.member?(4)))
    p([1,2,3].member?(1))
    p(!([1,2,3].member?(4)))