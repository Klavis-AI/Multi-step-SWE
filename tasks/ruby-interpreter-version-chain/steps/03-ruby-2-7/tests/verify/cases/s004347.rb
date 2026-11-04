class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
struct = Struct.new(:x, :y)
    obj = struct.new(1, 2)

    p(obj.deconstruct_keys(nil))