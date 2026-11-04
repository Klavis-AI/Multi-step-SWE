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

regexp = /(?<foo>foo )?(?<bar>bar)/
    full_match = regexp.match("foo bar")
    partial_match = regexp.match("bar")

    p(full_match[:foo])
    p(partial_match[:foo])

    p(full_match['foo'])
    p(partial_match['foo'])