class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(Enumerator.new { |y| y.yield([1]) }.to_a)
        p(Enumerator.new { |y| y.yield([1]) }.first)

        p(Enumerator.new { |y| y.yield([1, 2]) }.to_a)
        p(Enumerator.new { |y| y.yield([1, 2]) }.first)