class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(1.step(10))
    p(1.step(10, 5))

    p((1..10).step)
    p((1...10).step(8))

    # both have exclude_end? == false
    p((1..10).step(100))

    p(((1..10).step == (1..11).step))
    p(((1..10).step == (1...10).step))
    p(((1..10).step == (1..10).step(2)))