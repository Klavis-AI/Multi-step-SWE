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
p(!!("\u261c" =~ /^\p{Age=6.0}$/u))
    p(!!("\u261c" =~ /^\p{Age=1.1}$/u))
    assert_no_match(/^\P{age=6.0}$/u, "\u261c")

    p(!!("\u31f6" =~ /^\p{age=6.0}$/u))
    p(!!("\u31f6" =~ /^\p{age=3.2}$/u))
    assert_no_match(/^\p{age=3.1}$/u, "\u31f6")
    assert_no_match(/^\p{age=3.0}$/u, "\u31f6")
    assert_no_match(/^\p{age=1.1}$/u, "\u31f6")

    p(!!("\u2754" =~ /^\p{age=6.0}$/u))
    assert_no_match(/^\p{age=5.0}$/u, "\u2754")
    assert_no_match(/^\p{age=4.0}$/u, "\u2754")
    assert_no_match(/^\p{age=3.0}$/u, "\u2754")
    assert_no_match(/^\p{age=2.0}$/u, "\u2754")
    assert_no_match(/^\p{age=1.1}$/u, "\u2754")