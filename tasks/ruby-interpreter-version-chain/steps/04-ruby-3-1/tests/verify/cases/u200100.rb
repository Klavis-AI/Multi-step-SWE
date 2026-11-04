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
p([1,2,3,4,5,0].take(3))
    begin; [1,2].take(-1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p([1,2].take(1000000000))