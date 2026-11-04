class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((1..3).reverse_each.size)
        p((1...3).reverse_each.size)

        p((1..3.3).reverse_each.size)
        p((1...3.3).reverse_each.size)