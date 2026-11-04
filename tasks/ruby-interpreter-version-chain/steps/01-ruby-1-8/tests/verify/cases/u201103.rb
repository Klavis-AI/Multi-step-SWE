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
p("foobarbaz".match(/bar/).to_s)

    o = Regexp.new('foo')
    def o.match(x, y, z); x + y + z; end
    p("foo".match(o, "bar", "baz"))
    x = nil
    "foo".match(o, "bar", "baz") {|y| x = y }
    p(x)

    begin; "foo".match
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end