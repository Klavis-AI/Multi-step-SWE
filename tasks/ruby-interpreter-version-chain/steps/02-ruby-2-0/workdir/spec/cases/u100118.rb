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
p(@obj.any? {|x| x >= 3 })
    p(@obj.any? {|x| x > 3 })
    p(@obj.any?)
    p([false, false, false].any?)