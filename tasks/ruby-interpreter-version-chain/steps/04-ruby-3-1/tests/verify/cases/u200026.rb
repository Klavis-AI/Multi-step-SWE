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
p(@cls[ 1, 1, 3, 5 ] & @cls[ 1, 2, 3 ])
    p(@cls[ 1, 1, 3, 5 ] & @cls[ ])
    p(@cls[  ]           & @cls[ 1, 2, 3 ])
    p(@cls[ 1, 2, 3 ]    & @cls[ 4, 5, 6 ])