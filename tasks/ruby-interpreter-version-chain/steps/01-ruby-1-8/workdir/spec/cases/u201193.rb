def iter0
    yield
  end
def iter1(args)
    yield args
  end
def block_args_unleashed
    yield(1,2,3,4,5)
  end
p(iter0 { 1 })
    p(iter0 { 2 })