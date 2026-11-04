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
a = []
    b = a.uniq! {|v| v.even? }
    p(b)

    a = [1]
    b = a.uniq! {|v| v.even? }
    p(b)

    a = [1,3]
    b = a.uniq! {|v| v.even? }
    p(a)
    p(b)
    p(b.equal?(a))

    a = [1,2]
    b = a.uniq! {|v| v.even? }
    p(a)
    p(b)