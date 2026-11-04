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
a = [1,2,3,4,5]
    p(a.rotate!)
    p(a)
    p(a.rotate!(2))
    p(a.rotate!(-4))
    p(a.rotate!(13))
    p(a.rotate!(-13))
    a = [1]
    p(a.rotate!)
    p(a.rotate!(2))
    p(a.rotate!(-4))
    p(a.rotate!(13))
    p(a.rotate!(-13))
    a = []
    p(a.rotate!)
    p(a.rotate!(2))
    p(a.rotate!(-4))
    p(a.rotate!(13))
    p(a.rotate!(-13))
    a = [].freeze
    e = begin; a.rotate!
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(!!(e.message =~ /can't modify frozen/))
    a = [1,2,3]
    begin; a.rotate!(1, 1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end