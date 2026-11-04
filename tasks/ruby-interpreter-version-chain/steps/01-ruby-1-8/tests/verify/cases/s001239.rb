class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module EnumeratorSpecs
  class Feed
    def each
      ScratchPad << yield
      ScratchPad << yield
      ScratchPad << yield
    end
  end
end

@enum = 1.upto(3)
p(@enum.next)
    @enum.rewind
    @enum.rewind
    p(@enum.next)