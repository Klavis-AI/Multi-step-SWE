class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
a = 0
    b = 3.times do |i|
      2.times { a += 1 }
    end
    p(a)
    p(b)