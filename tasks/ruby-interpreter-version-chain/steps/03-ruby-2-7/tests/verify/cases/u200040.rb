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
    p(a.collect {|e| e.class})
    p(a.collect { 99 })

    p(@cls[].collect { 99 })

    # Ruby 1.9 feature change:
    # Enumerable#collect without block returns an Enumerator.
    #assert_equal([1, 2, 3], @cls[1, 2, 3].collect)
    p(@cls[1, 2, 3].collect.class)