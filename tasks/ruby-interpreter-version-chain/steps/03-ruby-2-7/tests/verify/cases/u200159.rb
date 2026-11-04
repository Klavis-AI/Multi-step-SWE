class MyObj
    def to_ary
      [[1,2],[3,4]]
    end
  end
A = 1
B = 2
class Base
    A = 3
    B = 4
  end
a = loop do break; end; p(a)
    a = loop do break nil; end; p(a)
    a = loop do break 1; end; p(a)
    a = loop do break []; end; p(a)
    a = loop do break [1]; end; p(a)
    a = loop do break [nil]; end; p(a)
    a = loop do break [[]]; end; p(a)
    a = loop do break [*[]]; end; p(a)
    a = loop do break [*[1]]; end; p(a)
    a = loop do break [*[1,2]]; end; p(a)

    a = loop do break *[]; end; p(a)
    a = loop do break *[1]; end; p(a)
    a = loop do break *[nil]; end; p(a)
    a = loop do break *[[]]; end; p(a)
    a = loop do break *[*[]]; end; p(a)
    a = loop do break *[*[1]]; end; p(a)
    a = loop do break *[*[1,2]]; end; p(a)

    *a = loop do break; end; p(a)
    *a = loop do break nil; end; p(a)
    *a = loop do break 1; end; p(a)
    *a = loop do break []; end; p(a)
    *a = loop do break [1]; end; p(a)
    *a = loop do break [nil]; end; p(a)
    *a = loop do break [[]]; end; p(a)
    *a = loop do break [1,2]; end; p(a)
    *a = loop do break [*[]]; end; p(a)
    *a = loop do break [*[1]]; end; p(a)
    *a = loop do break [*[1,2]]; end; p(a)

    *a = loop do break *[]; end; p(a)
    *a = loop do break *[1]; end; p(a)
    *a = loop do break *[nil]; end; p(a)
    *a = loop do break *[[]]; end; p(a)
    *a = loop do break *[1,2]; end; p(a)
    *a = loop do break *[*[]]; end; p(a)
    *a = loop do break *[*[1]]; end; p(a)
    *a = loop do break *[*[1,2]]; end; p(a)

    *a = *loop do break *[[]]; end; p(a)
    *a = *loop do break *[1,2]; end; p(a)
    *a = *loop do break *[*[1,2]]; end; p(a)

    a,b,*c = loop do break; end; p([a,b,c])
    a,b,*c = loop do break nil; end; p([a,b,c])
    a,b,*c = loop do break 1; end; p([a,b,c])
    a,b,*c = loop do break []; end; p([a,b,c])
    a,b,*c = loop do break [1]; end; p([a,b,c])
    a,b,*c = loop do break [nil]; end; p([a,b,c])
    a,b,*c = loop do break [[]]; end; p([a,b,c])
    a,b,*c = loop do break [1,2]; end; p([a,b,c])
    a,b,*c = loop do break [*[]]; end; p([a,b,c])
    a,b,*c = loop do break [*[1]]; end; p([a,b,c])
    a,b,*c = loop do break [*[1,2]]; end; p([a,b,c])

    a,b,*c = loop do break *[]; end; p([a,b,c])
    a,b,*c = loop do break *[1]; end; p([a,b,c])
    a,b,*c = loop do break *[nil]; end; p([a,b,c])
    a,b,*c = loop do break *[[]]; end; p([a,b,c])
    a,b,*c = loop do break *[1,2]; end; p([a,b,c])
    a,b,*c = loop do break *[*[]]; end; p([a,b,c])
    a,b,*c = loop do break *[*[1]]; end; p([a,b,c])
    a,b,*c = loop do break *[*[1,2]]; end; p([a,b,c])