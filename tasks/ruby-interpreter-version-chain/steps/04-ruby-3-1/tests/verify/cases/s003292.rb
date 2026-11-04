class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
p(-> *args, &blk do
      local_is_not_parameter = {}
    end.parameters)
    p(proc do |*args, &blk|
      local_is_not_parameter = {}
    end.parameters)