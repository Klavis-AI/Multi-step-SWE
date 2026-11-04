class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module MatchDataSpecs
  class MyString < String; end
end

regexp = /(?:
        A(?<word>\w+)
      |
        B(?<word>\w+)
    )/x
    md_a = regexp.match("Afoo")
    md_b = regexp.match("Bfoo")

    p(md_a[:word])
    p(md_b[:word])

    p(md_a['word'])
    p(md_b['word'])