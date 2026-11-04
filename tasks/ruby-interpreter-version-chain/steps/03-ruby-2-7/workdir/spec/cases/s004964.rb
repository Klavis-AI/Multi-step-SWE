class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
digits_regexp = /\d+/
    case "foo43"
    when /oo(#{digits_regexp})/
      p($~)
      p($1)
    else
      flunk
    end
    p($~)
    p($1)