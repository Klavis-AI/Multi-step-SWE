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
a = @cls[ 1, 2, 3]
    a_id = a.__id__
    p(a.replace(@cls[4, 5, 6]))
    p(a)
    p(a.__id__)
    p(a.replace(@cls[]))

    fa = a.dup.freeze
    begin; a.replace(a)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; fa.replace(fa)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; fa.replace()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; a.replace(42)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; fa.replace(42)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end