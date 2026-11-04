class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
original = /ll/.match("hello")
    original.instance_variable_set(:@custom_ivar, 42)
    duplicate = original.dup

    p(duplicate.instance_variable_get(:@custom_ivar))
    p(original.regexp)
    p(original.string)
    p(original.offset(0))