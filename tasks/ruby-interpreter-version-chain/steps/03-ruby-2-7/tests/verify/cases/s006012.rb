class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module LanguageSpecs
  # Regexp support

  def self.paired_delimiters
    [%w[( )], %w[{ }], %w[< >], ["[", "]"]]
  end

  def self.non_paired_delimiters
    %w[~ ! # $ % ^ & * _ + ` - = " ' , . ? / | \\]
  end

  def self.blanks
    " \t"
  end

  def self.white_spaces
    return blanks + "\f\n\r\v"
  end

  def self.non_alphanum_non_space
    '~!@#$%^&*()+-\|{}[]:";\'<>?,./'
  end

  def self.punctuations
    ",.?" # TODO - Need to fill in the full list
  end

  def self.get_regexp_with_substitution o
    /#{o}/o
  end
end

p(/foo\b/.match("foo").to_a)
    p(/foo\b/.match("foo\n").to_a)
    LanguageSpecs.white_spaces.scan(/./).each do |c|
      p(/foo\b/.match("foo" + c).to_a)
    end
    LanguageSpecs.non_alphanum_non_space.scan(/./).each do |c|
      p(/foo\b/.match("foo" + c).to_a)
    end
    p(/foo\b/.match("foo\0").to_a)
    # Basic non-matching
    p(/foo\b/.match("foobar"))
    p(/foo\b/.match("foo123"))
    p(/foo\b/.match("foo_"))