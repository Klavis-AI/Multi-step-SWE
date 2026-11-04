class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
/foo/ =~ "TEST123"
    p(Regexp.last_match(:test))
    p(Regexp.last_match(1))
    p(Regexp.last_match(Object.new))
    p(Regexp.last_match("test"))