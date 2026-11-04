class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
mod = Module.new
    mod.const_set :Foo, true
    mod.const_set :Bar, true

    mod.send :private_constant, :Foo
    mod.send :private_constant, :Bar

    mod.send :public_constant, :Foo, :Bar

    p(mod::Foo)
    p(mod::Bar)