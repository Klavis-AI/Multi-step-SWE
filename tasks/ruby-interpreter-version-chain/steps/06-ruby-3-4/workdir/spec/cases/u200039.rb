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
for taint in [ false, true ]
      for untrust in [ false, true ]
        for frozen in [ false, true ]
          a = @cls[*(0..99).to_a]
          a.taint  if taint
          a.untrust  if untrust
          a.freeze if frozen
          b = a.clone

          p(b)
          p(b.__id__)
          p(b.frozen?)
          p(b.untrusted?)
          p(b.tainted?)
        end
      end
    end