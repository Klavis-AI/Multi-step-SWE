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

p(/^foo/.match("foo").to_a)
    p(/^bar/.match("foo\nbar").to_a)
    # Basic non-matching
    p(/^foo/.match(" foo"))
    p(/foo^/.match("foo\n\n\n"))

    # A bit advanced
    p(/^^^foo/.match("foo").to_a)
    p((/^[^f]/ =~ "foo\n\n")) and p($~.to_a)
    p((/($^)($^)/ =~ "foo\n\n")) and p($~.to_a)

    # Different start of line chars
    p(/^bar/.match("foo\rbar"))
    p(/^bar/.match("foo\0bar"))

    # Trivial
    p(/^/.match("foo").to_a)

    # Grouping
    p(/(^foo)/.match("foo").to_a)
    p(/(^)/.match("foo").to_a)
    p(/(foo\n^)(^bar)/.match("foo\nbar").to_a)