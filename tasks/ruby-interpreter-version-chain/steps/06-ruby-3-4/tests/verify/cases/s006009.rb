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

p(/\Afoo/.match("foo").to_a)
    # Basic non-matching
    p(/\Abar/.match("foo\nbar"))
    p(/\Afoo/.match(" foo"))

    # A bit advanced
    p(/\A\A\Afoo/.match("foo").to_a)
    p(/(\A\Z)(\A\Z)/.match("").to_a)

    # Different start of line chars
    p(/\Abar/.match("foo\0bar"))

    # Grouping
    p(/(\Afoo)/.match("foo").to_a)
    p(/(\A)/.match("foo").to_a)