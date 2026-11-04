class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((0..2).overlap?(3..4))
    p((0..2).overlap?(-4..-1))

    p(('a'..'c').overlap?('d'..'f'))