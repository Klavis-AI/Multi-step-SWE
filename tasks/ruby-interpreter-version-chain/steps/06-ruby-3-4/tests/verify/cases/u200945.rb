# coding: US-ASCII
def check(re, ss, fs = [], msg = nil)
    re = Regexp.new(re) unless re.is_a?(Regexp)
    ss = [ss] unless ss.is_a?(Array)
    ss.each do |e, s|
      s ||= e
      assert_match(re, s, msg)
      m = re.match(s)
      assert_equal(e, m[0], msg)
    end
    fs = [fs] unless fs.is_a?(Array)
    fs.each {|s| assert_no_match(re, s, msg) }
  end
def failcheck(re)
    assert_raise(RegexpError) { %r"#{ re }" }
  end
def assert_match_each(re, conds, msg = nil)
    errs = conds.select {|str, match| match ^ (re =~ str)}
    msg = message(msg) {
      "Expected #{re.inspect} to\n" +
      errs.map {|str, match| "\t#{'not ' unless match}match #{str.inspect}"}.join(",\n")
    }
    assert(errs.empty?, msg)
  end
@verbose = $VERBOSE
    $VERBOSE = nil
m = /&(?<foo>.*?);/.match("aaa &amp; yyy")
    p(m["foo"])
    p(m[:foo])
    p(m.begin(:foo))
    p(m.end(:foo))
    p(m.offset(:foo))

    p("aaa &amp; yyy".sub(/&(?<foo>.*?);/, '[\k<foo>]'))

    p(/&(?<foo>.*?); (y)/.match("aaa &amp; yyy").inspect)
    p(/&(.*?); (y)/.match("aaa &amp; yyy").inspect)
    p(/&(?<foo>.*?); (?<bar>y)/.match("aaa &amp; yyy").inspect)
    p(/&(?<foo>.*?); (?<foo>y)/.match("aaa &amp; yyy").inspect)

    /(?<id>[A-Za-z_]+)/ =~ "!abc"
    p(Regexp.last_match(:id))

    /a/ =~ "b" # doesn't match.
    p(Regexp.last_match)
    p(Regexp.last_match(1))
    p(Regexp.last_match(:foo))

    p(/(?<foo>.)(?<bar>.)/.names)
    p(/(?<foo>.)(?<foo>.)/.names)
    p(/(.)(.)/.names)

    p(/(?<foo>.)(?<bar>.)/.match("ab").names)
    p(/(?<foo>.)(?<foo>.)/.match("ab").names)
    p(/(.)(.)/.match("ab").names)

    p(/(?<foo>.)(?<bar>.)/.named_captures)
    p(/(?<foo>.)(?<foo>.)/.named_captures)
    p(/(.)(.)/.named_captures)

    p("abc".sub(/(?<x>[bc])/, "[\\k<x>]"))

    p("foo"[/(?<bar>o)/, "bar"])

    s = "foo"
    s[/(?<bar>o)/, "bar"] = "baz"
    p(s)