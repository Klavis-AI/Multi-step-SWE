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
p(@obj.inject {|z, x| z * x })
    p(@obj.inject {|z, x| z * 2 + x })
    p(@obj.inject(:*))
    p(@obj.inject(2) {|z, x| z * x })
    p(@obj.inject(2, :*) {|z, x| z * x })