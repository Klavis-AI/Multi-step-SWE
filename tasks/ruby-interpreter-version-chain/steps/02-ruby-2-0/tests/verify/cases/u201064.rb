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
p($;)
    p(S(" a   b\t c ").split)
    p(S(" a   b\t c ").split(S(" ")))

    p(S(" a | b | c ").split(S("|")))

    p(S("aXXbXXcXX").split(/X./))

    p(S("abc").split(//))

    p(S("a|b|c").split(S('|'), 1))

    p(S("a|b|c").split(S('|'), 2))
    p(S("a|b|c").split(S('|'), 3))

    p(S("a|b|c|").split(S('|'), -1))
    p(S("a|b|c||").split(S('|'), -1))

    p(S("a||b|c|").split(S('|')))
    p(S("a||b|c|").split(S('|'), -1))

    p("".split(//, 1))

    p([1,2,3].slice!(1,10000).inspect)

    bug6206 = '[ruby-dev:45441]'
    Encoding.list.each do |enc|
      next unless enc.ascii_compatible?
      s = S("a:".force_encoding(enc))
      p(s.split(":", 2).map(&:encoding))
    end