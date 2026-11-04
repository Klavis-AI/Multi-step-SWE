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
a = [0]
    p(a.insert(1))
    p(a.insert(1, 1))
    begin; a.insert
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(a.insert(-1, 2))
    p(a.insert(-2, 3))
    begin; [0].freeze.insert(0)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; [0].freeze.insert
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end