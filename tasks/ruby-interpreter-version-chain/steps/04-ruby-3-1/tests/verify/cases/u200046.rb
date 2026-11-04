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
a = @cls[*(1..5).to_a]
    p(a.delete_at(2))
    p(a)

    a = @cls[*(1..5).to_a]
    p(a.delete_at(-2))
    p(a)

    a = @cls[*(1..5).to_a]
    p(a.delete_at(5))
    p(a)

    a = @cls[*(1..5).to_a]
    p(a.delete_at(-6))
    p(a)