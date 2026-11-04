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
a = @cls[ 4, 1, 2, 3 ]
    p(a.sort)
    p(a)

    p(a.sort { |x, y| y <=> x})
    p(a)

    p(a.sort { |x, y| (x - y) * (2**100) })

    a.fill(1)
    p(a.sort)

    p(@cls[].sort)