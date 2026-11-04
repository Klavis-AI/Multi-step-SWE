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
a = @cls[ 1, 'cat', 1..1 ]
    p(a.collect! {|e| e.class})
    p(a)

    a = @cls[ 1, 'cat', 1..1 ]
    p(a.collect! { 99 })
    p(a)

    a = @cls[ ]
    p(a.collect! { 99 })
    p(a)