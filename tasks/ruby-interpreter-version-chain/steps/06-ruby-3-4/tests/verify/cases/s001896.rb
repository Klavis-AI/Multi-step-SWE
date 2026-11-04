class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@hash = { a: 1, b: 2, c: 3 }
enumerator = @hash.transform_values
      p(enumerator)
      p(enumerator.size)
      p(enumerator.each(&:succ))