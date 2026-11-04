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
p(@obj.cycle.take(10))