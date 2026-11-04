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
a = [0, 4, 7, 10, 12]
    p([4, 7].include?(a.bsearch {|x| 1 - x / 4 }))
    p(a.bsearch {|x| 4 - x / 2 })
    p(a.bsearch {|x| 1 })
    p(a.bsearch {|x| -1 })

    p([4, 7].include?(a.bsearch {|x| (1 - x / 4) * (2**100) }))
    p(a.bsearch {|x|   1  * (2**100) })
    p(a.bsearch {|x| (-1) * (2**100) })

    p([4, 7].include?(a.bsearch {|x| (2**100).coerce((1 - x / 4) * (2**100)).first }))