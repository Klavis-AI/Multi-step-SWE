def iter0
    yield
  end
def iter1(args)
    yield args
  end
def block_args_unleashed
    yield(1,2,3,4,5)
  end
def iter2_1() yield 1, *[2, 3] end
    iter2_1 {|a, b, c| p([a,b,c]) }
    def iter2_2() yield 1, *[] end
    iter2_2 {|a, b, c| p([a,b,c]) }
    def iter2_3() yield 1, *[2] end
    iter2_3 {|a, b, c| p([a,b,c]) }