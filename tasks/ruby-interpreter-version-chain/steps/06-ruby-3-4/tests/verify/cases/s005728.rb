class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(-> { _1 }.arity)
    p(-> { _2 }.arity)
    p(-> { _3 }.arity)
    p(-> { _4 }.arity)
    p(-> { _5 }.arity)
    p(-> { _6 }.arity)
    p(-> { _7 }.arity)
    p(-> { _8 }.arity)
    p(-> { _9 }.arity)

    p(->     { _9 }.arity)
    p(proc   { _9 }.arity)
    p(lambda { _9 }.arity)