class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
h = { a: [1, 2], b: -2, d: -6, nil => nil }
    expected = ruby_version_is("3.4") ? "{a: [1, 2], b: -2, d: -6, nil => nil}" : "{:a=>[1, 2], :b=>-2, :d=>-6, nil=>nil}"
    p(h.inspect)