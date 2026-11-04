class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@hash = { a: 1, b: 2, c: 3 }
value = [1, 2, 3]
    new_hash = { a: value }.transform_keys(&:upcase)
    p(new_hash[:A])