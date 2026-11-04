class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@h = {apple: :green, orange: :orange, grape: :green, banana: :yellow}
@h[:key] = 1.0
    p(1)
    p(@h.rassoc(1))