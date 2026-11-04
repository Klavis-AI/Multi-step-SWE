class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
[ [:'foo bar',      ':"foo bar"'],
      [:'++',           ':"++"'],
      [:'9',            ':"9"'],
      [:"foo #{1 + 1}", ':"foo 2"'],
      [:"foo\nbar",     ':"foo\nbar"'],
    ].each { |sym, str|
      p(sym)
      p(sym.inspect)
    }