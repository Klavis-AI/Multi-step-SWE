class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(:rubinius.to_s)
    p(:squash.to_s)
    p(:[].to_s)
    p(:@ruby.to_s)
    p(:@@ruby.to_s)