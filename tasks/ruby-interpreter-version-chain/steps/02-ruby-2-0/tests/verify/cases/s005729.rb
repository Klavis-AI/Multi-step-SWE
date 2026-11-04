class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(-> { _1 }.parameters)
    p(-> { _2 }.parameters)

    p(proc { _1 }.parameters)
    p(proc { _2 }.parameters)