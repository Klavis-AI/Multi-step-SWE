def iter0
    yield
  end
def iter1(args)
    yield args
  end
def block_args_unleashed
    yield(1,2,3,4,5)
  end
h = {:a => 1}
    h.each do |k, v|
      p(k)
      p(v)
    end
    h.each do |kv|
      p(kv)
    end