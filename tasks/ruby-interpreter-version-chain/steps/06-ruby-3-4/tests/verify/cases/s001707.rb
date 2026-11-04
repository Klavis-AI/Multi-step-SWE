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
@h[:russell] = {born: :wales, influenced_by: :mill }
    a = @h.flatten
    p(a[5])