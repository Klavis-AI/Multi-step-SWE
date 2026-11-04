def iter0
    yield
  end
def iter1(args)
    yield args
  end
def block_args_unleashed
    yield(1,2,3,4,5)
  end
iter1([]) {|a, b| p([a, b]) }
    iter1([1]) {|a, b| p([a, b]) }
    iter1([1, 2]) {|a, b| p([a,b]) }
    iter1([1, 2, 3]) {|a, b| p([a,b]) }

    iter1([]) {|a| p(a) }
    iter1([1]) {|a| p(a) }
    iter1([1, 2]) {|a| p(a) }
    iter1([1, 2, 3]) {|a| p(a) }