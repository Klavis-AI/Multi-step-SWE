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

class DefSpecs
      def foo(x = (def foo; "hello"; end;1));x;end
    end

    d = DefSpecs.new
    p(d.foo(42))
    p(d.foo)
    p(d.foo)