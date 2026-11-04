class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def test(v)
      case new_variable_in_expression = v
      when true
        # This extra block is a test that `new_variable_in_expression` is declared outside of it and not inside
        self.then { new_variable_in_expression }
      else
        # Same
        self.then { new_variable_in_expression.casecmp?("foo") }
      end
    end

    p(self.test("bar"))
    p(self.test(true))