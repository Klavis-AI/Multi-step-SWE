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
a1 = @cls[ 1, 2, 3]
    a2 = @cls[ 5, 6 ]
    a3 = @cls[ 4, a2 ]
    a4 = @cls[ a1, a3 ]
    p(a4.flatten)
    p(a4)

    a5 = @cls[ a1, @cls[], a3 ]
    p(a5.flatten)
    p(@cls[].flatten)
    p(@cls[@cls[@cls[@cls[],@cls[]],@cls[@cls[]],@cls[]],@cls[@cls[@cls[]]]].flatten)

    begin; [[]].flatten("")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    a6 = @cls[[1, 2], 3]
    a6.taint
    a6.untrust
    a7 = a6.flatten
    p(a7.tainted?)
    p(a7.untrusted?)

    a8 = @cls[[1, 2], 3]
    a9 = a8.flatten(0)
    p(a9)
    p(a9.equal?(a8))