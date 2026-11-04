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

p(/foo\Z/.match("foo").to_a)
    p(/foo\Z/.match("foo\n").to_a)
    # Basic non-matching
    p(/foo\Z/.match("foo\nbar"))
    p(/foo\Z/.match("foo "))

    # A bit advanced
    p(/foo\Z\Z\Z/.match("foo\n").to_a)
    p((/($\Z)($\Z)/ =~ "foo\n")) and p($~.to_a)
    p((/(\z\Z)(\z\Z)/ =~ "foo\n")) and p($~.to_a)

    # Different end of line chars
    p(/foo\Z/.match("foo\0bar"))
    p(/foo\Z/.match("foo\r\n"))

    # Grouping
    p(/(foo\Z)/.match("foo").to_a)
    p((/(\Z)/ =~ "foo")) and p($~.to_a)