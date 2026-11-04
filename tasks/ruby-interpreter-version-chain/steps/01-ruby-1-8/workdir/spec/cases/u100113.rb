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
h = { 1 => [1, 1], 2 => [2, 2], 3 => [3] }
    p(@obj.group_by {|x| x })