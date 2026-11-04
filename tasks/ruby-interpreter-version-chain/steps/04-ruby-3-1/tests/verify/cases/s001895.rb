class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@hash = { a: 1, b: 2, c: 3 }
key = [1, 2, 3]
    new_hash = { key => 1 }.transform_values(&:succ)
    p(new_hash[key])
    p(new_hash.keys[0])