class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@hash = { a: 1, b: 2, c: 3 }
klass = Class.new(Hash)
    h = klass.new
    h[:foo] = 42
    r = h.transform_keys{|v| :"x#{v}"}
    p(r.keys)
    p(r.class)