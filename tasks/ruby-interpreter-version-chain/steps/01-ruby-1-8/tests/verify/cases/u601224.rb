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
obj = Object.new
    def obj.each
      yield(*[])
    end
    obj.each{|*v| p([])}
    obj.to_enum.each{|*v| p([])}