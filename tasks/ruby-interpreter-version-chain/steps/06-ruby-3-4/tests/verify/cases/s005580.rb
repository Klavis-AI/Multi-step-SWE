class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def y(*args, **kwargs)
        yield(*args, **kwargs)
      end

      empty = {}
      p(y(**empty) { |*a| a })
      p(y(empty) { |*a| a })