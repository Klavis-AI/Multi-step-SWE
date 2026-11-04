class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
outer_loop = 0
    loop do
      outer_loop += 1
      break if outer_loop == 10
    end
    p(outer_loop)