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
a = @cls[ 'cat', 99, /a/, @cls[ 1, 2, 3] ]
    p(a.include?('cat'))
    p(a.include?(99))
    p(a.include?(/a/))
    p(a.include?([1,2,3]))
    assert_not_include(a, 'ca')
    assert_not_include(a, [1,2])