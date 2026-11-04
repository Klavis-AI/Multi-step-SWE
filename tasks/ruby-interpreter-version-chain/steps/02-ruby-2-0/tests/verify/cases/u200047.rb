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
a = @cls[ 1, 2, 3, 4, 5 ]
    p(a.delete_if { false })
    p(a)

    a = @cls[ 1, 2, 3, 4, 5 ]
    p(a.delete_if { true })
    p(a)

    a = @cls[ 1, 2, 3, 4, 5 ]
    p(a.delete_if { |i| i > 3 })
    p(a)

    bug2545 = '[ruby-core:27366]'
    a = @cls[ 5, 6, 7, 8, 9, 10 ]
    p(a.delete_if {|i| break i if i > 8; assert_equal(a[0], i) || true if i < 7})
    p(a)