def initialize(*args)
    @cls = String
    @aref_re_nth = true
    @aref_re_silent = false
    @aref_slicebang_silent = true
    super
  end
def S(str)
    @cls.new(str)
  end
def casetest(a, b, rev=false)
    case a
      when b
        assert(!rev)
      else
        assert(rev)
    end
  end
Bug2463 = '[ruby-dev:39856]'
def check_sum(str, bits=16)
    sum = 0
    str.each_byte {|c| sum += c}
    sum = sum & ((1 << bits) - 1) if bits != 0
    assert_equal(sum, str.sum(bits))
  end
class S2 < String
  end
class StringLike
    def initialize(str)
      @str = str
    end

    def to_str
      @str
    end
  end
def u(str)
    str.force_encoding(Encoding::UTF_8)
  end
p("hello".byteslice(0))
    p("hello".byteslice(5))
    p("hello".byteslice(-1))
    p("hello".byteslice(-6))

    p("hello".byteslice(0, 0))
    p("hello".byteslice(0, 6))
    p("hello".byteslice(0, 6))
    p("hello".byteslice(5, 1))
    p("hello".byteslice(-1, 6))
    p("hello".byteslice(-6, 1))
    p("hello".byteslice(0, -1))

    p("hello".byteslice(0..0))
    p("hello".byteslice(5..0))
    p("hello".byteslice(4..5))
    p("hello".byteslice(6..0))
    p("hello".byteslice(-1..0))
    p("hello".byteslice(-3..5))

    p("\u3042".byteslice(1))
    p("\u3042".byteslice(1, 2))
    p("\u3042".byteslice(1..2))

    p(("\u3042"*10).byteslice(2, 28))

    bug7954 = '[ruby-dev:47108]'
    p("\u3042".byteslice(0, 2).valid_encoding?)
    p(("\u3042"*10).byteslice(0, 20).valid_encoding?)