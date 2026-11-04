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
begin; [].freeze.unshift('cat')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; [].freeze.unshift()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end