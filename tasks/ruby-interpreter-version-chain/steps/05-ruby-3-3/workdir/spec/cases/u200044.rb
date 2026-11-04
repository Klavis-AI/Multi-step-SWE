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
p(@cls[1, 2].concat(@cls[3, 4]))
    p(@cls[].concat(@cls[1, 2, 3, 4]))
    p(@cls[1, 2, 3, 4].concat(@cls[]))
    p(@cls[].concat(@cls[]))
    p(@cls[@cls[1, 2]].concat(@cls[@cls[3, 4]]))

    a = @cls[1, 2, 3]
    a.concat(a)
    p(a)

    begin; [0].concat(:foo)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; [0].freeze.concat(:foo)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end