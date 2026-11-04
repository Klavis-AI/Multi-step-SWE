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

def foo(a = 1)
      a
    end
    p(foo)
    p(foo(2))