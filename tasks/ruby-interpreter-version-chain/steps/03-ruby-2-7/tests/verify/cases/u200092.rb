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
a = []
    b = a.uniq!
    p(b)

    a = [1]
    b = a.uniq!
    p(b)

    a = [1,1]
    b = a.uniq!
    p(a)
    p(b)
    p(b.equal?(a))

    a = [1,2]
    b = a.uniq!
    p(a)
    p(b)

    a = @cls[ 1, 2, 3, 2, 1, 2, 3, 4, nil ]
    p(a.uniq!)
    p(a)

    c = @cls["a:def", "a:xyz", "b:abc", "b:xyz", "c:jkl"]
    p(c.uniq! {|s| s[/^\w+/]})
    p(c)

    c = @cls["a:def", "b:abc", "c:jkl"]
    p(c.uniq! {|s| s[/^\w+/]})
    p(c)

    p(@cls[1, 2, 3].uniq!)

    f = a.dup.freeze
    begin; a.uniq!(1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; f.uniq!(1)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; f.uniq!
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    begin; a = [ {c: "b"}, {c: "r"}, {c: "w"}, {c: "g"}, {c: "g"} ]
      a.sort_by!{|e| e[:c]}
      a.uniq!   {|e| e[:c]}
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end