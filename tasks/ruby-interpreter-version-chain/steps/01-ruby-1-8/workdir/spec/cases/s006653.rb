class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
i = 0
    begin
      i += 1
      break if i > 5
    end while i < 10

    p(i)