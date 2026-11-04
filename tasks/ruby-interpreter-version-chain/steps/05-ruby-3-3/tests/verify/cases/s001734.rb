class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
expected = ruby_version_is("3.4") ? "{true => false}" : "{true=>false}"
    p({ true => false }.inspect)