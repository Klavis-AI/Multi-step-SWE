class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def toplevel_define_other_method
  def nested_method_in_toplevel_method
    42
  end
end

def some_toplevel_method
end

public
def public_toplevel_method
end

private

$__a = Object.new
    def $__a.foo(x = (def $__a.foo; "hello"; end;1));x;end

    p($__a.foo(42))
    p($__a.foo)
    p($__a.foo)