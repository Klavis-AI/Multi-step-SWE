class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
x = {}
    x[0] = x
    expected = ruby_version_is("3.4") ? '{0 => {...}}' : '{0=>{...}}'
    p(x.inspect)

    x = {}
    y = {}
    x[0] = y
    y[1] = x
    expected_x = ruby_version_is("3.4") ? '{0 => {1 => {...}}}' : '{0=>{1=>{...}}}'
    expected_y = ruby_version_is("3.4") ? '{1 => {0 => {...}}}' : '{1=>{0=>{...}}}'
    p(x.inspect)
    p(y.inspect)