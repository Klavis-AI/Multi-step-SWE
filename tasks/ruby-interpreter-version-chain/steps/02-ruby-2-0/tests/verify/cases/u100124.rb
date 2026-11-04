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
p(@obj.min_by {|x| -x })
    a = %w(albatross dog horse)
    p(a.min_by {|x| x.length })
    p([2,3,1].min_by {|x| -x })