class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@h = {plato: :greek,
          witgenstein: [:austrian, :british],
          russell: :welsh}
a = @h.flatten
    p(a[1])
    p(a[3])
    p(a[5])