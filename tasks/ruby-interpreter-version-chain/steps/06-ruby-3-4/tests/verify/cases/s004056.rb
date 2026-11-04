class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
str = +"hello"
      str.append_as_bytes("\xE2\x82")
      p(str.valid_encoding?)

      str.append_as_bytes("\xAC")
      p(str.valid_encoding?)