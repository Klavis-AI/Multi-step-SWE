class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(2.5.round(half: nil))
    p(2.5.round(half: :up))
    p(2.5.round(half: :down))
    p(2.5.round(half: :even))
    p(3.5.round(half: nil))
    p(3.5.round(half: :up))
    p(3.5.round(half: :down))
    p(3.5.round(half: :even))
    p((-2.5).round(half: nil))
    p((-2.5).round(half: :up))
    p((-2.5).round(half: :down))
    p((-2.5).round(half: :even))