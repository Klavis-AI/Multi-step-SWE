class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p({ a: 1, b: 'c' }.size)
    h = { a: 1, b: 2 }
    h[:a] = 2
    p(h.size)
    p({ a: 1, b: 1, c: 1 }.size)
    p({}.size)
    p(Hash.new(5).size)
    p(Hash.new { 5 }.size)