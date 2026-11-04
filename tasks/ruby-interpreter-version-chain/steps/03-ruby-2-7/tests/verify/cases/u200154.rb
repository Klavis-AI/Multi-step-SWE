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
a=[]; a[0] ||= "bar";
    p(a[0])
    h={}; h["foo"] ||= "bar";
    p(h["foo"])

    aa = 5
    aa ||= 25
    p(aa)
    bb ||= 25
    p(bb)
    cc &&=33
    p(cc)
    cc = 5
    cc &&=44
    p(cc)

    a = nil; p(a)
    a = 1; p(a)
    a = []; p(a)
    a = [1]; p(a)
    a = [nil]; p(a)
    a = [[]]; p(a)
    a = [1,2]; p(a)
    a = [*[]]; p(a)
    a = [*[1]]; p(a)
    a = [*[1,2]]; p(a)

    a = *[]; p(a)
    a = *[1]; p(a)
    a = *[nil]; p(a)
    a = *[[]]; p(a)
    a = *[1,2]; p(a)
    a = *[*[]]; p(a)
    a = *[*[1]]; p(a)
    a = *[*[1,2]]; p(a)

    *a = nil; p(a)
    *a = 1; p(a)
    *a = []; p(a)
    *a = [1]; p(a)
    *a = [nil]; p(a)
    *a = [[]]; p(a)
    *a = [1,2]; p(a)
    *a = [*[]]; p(a)
    *a = [*[1]]; p(a)
    *a = [*[1,2]]; p(a)

    *a = *[]; p(a)
    *a = *[1]; p(a)
    *a = *[nil]; p(a)
    *a = *[[]]; p(a)
    *a = *[1,2]; p(a)
    *a = *[*[]]; p(a)
    *a = *[*[1]]; p(a)
    *a = *[*[1,2]]; p(a)

    a,b,*c = nil; p([a,b,c])
    a,b,*c = 1; p([a,b,c])
    a,b,*c = []; p([a,b,c])
    a,b,*c = [1]; p([a,b,c])
    a,b,*c = [nil]; p([a,b,c])
    a,b,*c = [[]]; p([a,b,c])
    a,b,*c = [1,2]; p([a,b,c])
    a,b,*c = [*[]]; p([a,b,c])
    a,b,*c = [*[1]]; p([a,b,c])
    a,b,*c = [*[1,2]]; p([a,b,c])

    a,b,*c = *[]; p([a,b,c])
    a,b,*c = *[1]; p([a,b,c])
    a,b,*c = *[nil]; p([a,b,c])
    a,b,*c = *[[]]; p([a,b,c])
    a,b,*c = *[1,2]; p([a,b,c])
    a,b,*c = *[*[]]; p([a,b,c])
    a,b,*c = *[*[1]]; p([a,b,c])
    a,b,*c = *[*[1,2]]; p([a,b,c])

    bug2050 = '[ruby-core:25629]'
    a = Hash.new {[]}
    b = [1, 2]
    p(a[:x] += [*b, 3])
    p(a[:x])
    p(a[:x] <<= [*b, 3])
    p(a[:x])