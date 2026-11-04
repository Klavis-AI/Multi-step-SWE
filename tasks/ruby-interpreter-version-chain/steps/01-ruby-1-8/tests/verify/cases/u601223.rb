# frozen_string_literal: false
def iter0
    yield
  end
def iter1(args)
    yield args
  end
def block_args_unleashed
    yield(1,2,3,4,5)
  end
[[1, [2, 3]]].each {|a, (b, c)|
      p([a,b,c])
    }
    [[1, [2, 3]]].map {|a, (b, c)|
      p([a,b,c])
    }