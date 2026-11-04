# frozen_string_literal: false
class DuckRange
    def initialize(b,e,excl=false)
      @begin = b
      @end = e
      @excl = excl
    end
    attr_reader :begin, :end

    def exclude_end?
      @excl
    end
  end
class CyclicRange < Range
    def <=>(other); true; end
  end
def check_bsearch_values(range, search, a)
    from, to = range.begin, range.end
    cmp = range.exclude_end? ? :< : :<=
    r = nil

    a.for "(0) trivial test" do
      r = Range.new(to, from, range.exclude_end?).bsearch do |x|
        fail "#{to}, #{from}, #{range.exclude_end?}, #{x}"
      end
      assert_nil r

      r = (to...to).bsearch do
        fail
      end
      assert_nil r
    end

    # prepare for others
    yielded = []
    r = range.bsearch do |val|
      yielded << val
      val >= search
    end

    a.for "(1) log test" do
      max = case from
            when Float then 65
            when Integer then Math.log(to-from+(range.exclude_end? ? 0 : 1), 2).to_i + 1
            end
      assert_operator yielded.size, :<=, max
    end

    a.for "(2) coverage test" do
      expect = case
               when search < from
                 from
               when search.send(cmp, to)
                 search
               else
                 nil
               end
      assert_equal expect, r
    end

    a.for "(3) uniqueness test" do
      assert_nil yielded.uniq!
    end

    a.for "(4) end of range test" do
      case
      when range.exclude_end?
        assert_not_include yielded, to
        assert_not_equal r, to
      when search >= to
        assert_include yielded, to
        assert_equal search == to ? to : nil, r
      end
    end

    a.for "(5) start of range test" do
      if search <= from
        assert_include yielded, from
        assert_equal from, r
      end
    end

    a.for "(6) out of range test" do
      yielded.each do |val|
        assert_operator from, :<=, val
        assert_send [val, cmp, to]
      end
    end
  end
a = []
    (1..3).reverse_each {|x| a << x }
    p(a)

    a = []
    (1...3).reverse_each {|x| a << x }
    p(a)

    fmax = RbConfig::LIMITS['FIXNUM_MAX']
    fmin = RbConfig::LIMITS['FIXNUM_MIN']

    a = []
    (fmax+1..fmax+3).reverse_each {|x| a << x }
    p(a)

    a = []
    (fmax+1...fmax+3).reverse_each {|x| a << x }
    p(a)

    a = []
    (fmax-1..fmax+1).reverse_each {|x| a << x }
    p(a)

    a = []
    (fmax-1...fmax+1).reverse_each {|x| a << x }
    p(a)

    a = []
    (fmin-1..fmin+1).reverse_each{|x| a << x }
    p(a)

    a = []
    (fmin-1...fmin+1).reverse_each{|x| a << x }
    p(a)

    a = []
    (fmin-3..fmin-1).reverse_each{|x| a << x }
    p(a)

    a = []
    (fmin-3...fmin-1).reverse_each{|x| a << x }
    p(a)

    a = []
    ("a".."c").reverse_each {|x| a << x }
    p(a)