class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
def m(x)
        if block_given?
          [true, yield(x + 'b')]
        else
          [false]
        end
      end
p(m('a', &nil))