class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
{ 0 => [0, -0, +0], 2 => [2, -2, +2], 100 => [100, -100, +100] }.each do |key, values|
        values.each do |value|
          p(value.abs)
        end
      end