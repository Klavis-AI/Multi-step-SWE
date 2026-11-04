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
r = block_args_unleashed {|a,b=1,*c,d,e|
      [a,b,c,d,e]
    }
    p(r)