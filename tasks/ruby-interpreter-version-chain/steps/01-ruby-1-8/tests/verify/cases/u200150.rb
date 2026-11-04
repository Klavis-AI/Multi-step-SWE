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
begin; [1, 2, 42, 100, 666].bsearch{ "not ok" }
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p([1, 2, 42, 100, 666].bsearch{false})