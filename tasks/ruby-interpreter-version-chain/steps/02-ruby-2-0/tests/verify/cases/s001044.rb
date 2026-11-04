class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
ScratchPad.record []

    sequence = [1, 2, 3]
    enum_lazy = Enumerator::Lazy.new(sequence) do |yielder, value|
      yielder << value
      ScratchPad << value
    end

    p(ScratchPad.recorded)
    enum = enum_lazy.eager
    p(ScratchPad.recorded)

    p(enum.map { |i| i })
    p(ScratchPad.recorded)