class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@enum = [:a, :b].to_enum
    @memo = ''
    @block_params = @enum.each_with_object(@memo).to_a
ret = @enum.each_with_object(@memo) do |elm, memo|
        # nothing
      end
      p(ret)