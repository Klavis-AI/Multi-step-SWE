class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@hash = { a: 1, b: 2, c: 3 }
pr = proc { |h, k| h[k] = [] }
    h = Hash.new(&pr)
    p(h.except(:a).default_proc)
    h[:a] = 1
    p(h.except(:a).default_proc)