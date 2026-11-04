class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(*args)
      args
    end

    def n(value, &block)
      [value, block.call]
    end
args = m ((0; 1))
      p(args)