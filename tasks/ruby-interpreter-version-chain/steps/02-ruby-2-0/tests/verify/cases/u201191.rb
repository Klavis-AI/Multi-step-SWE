def iter0
    yield
  end
def iter1(args)
    yield args
  end
def block_args_unleashed
    yield(1,2,3,4,5)
  end
ary = [1]
    ary.each {|a, b, c, d| p([a,b,c,d]) }
    ary.each {|a, b, c| p([a,b,c]) }
    ary.each {|a, b| p([a,b]) }
    ary.each {|a| p(a) }