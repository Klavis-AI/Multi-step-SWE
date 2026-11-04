# coding: US-ASCII
LONGP = [127, 63, 31, 15, 7].map {|x| 2**x-1 }.find do |x|
    begin
      [].first(x)
    rescue ArgumentError
      true
    rescue RangeError
      false
    end
  end
A = Array.new(3, &:to_s)
B = A.dup
class Array2 < Array
  end
@verbose = $VERBOSE
    $VERBOSE = nil
    @cls = Array
a = [ 1, 2, 3 ]
    b = @cls[*a]

    a_id = a.__id__
    p(b.to_ary)
    if (@cls == Array)
      p(a.to_ary.__id__)
    end

    o = Object.new
    def o.to_ary
      [4, 5]
    end
    p(a.concat(o))

    o = Object.new
    def o.to_ary
      foo_bar()
    end
    p(!!(assert_raise(NoMethodError) {a.concat(o)}.message =~ /foo_bar/))