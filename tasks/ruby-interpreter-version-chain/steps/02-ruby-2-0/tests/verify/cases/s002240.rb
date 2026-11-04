class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p((256**7).size)
      p((256**8).size)
      p((256**9).size)
      p((256**10).size)
      p((256**10-1).size)
      p((256**11).size)
      p((256**12).size)
      p((256**20-1).size)
      p((256**40-1).size)