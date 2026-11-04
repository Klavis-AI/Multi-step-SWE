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
m = /(?<x>b..)/.match("foobarbaz")
    p(m.offset("x"))
    p(m.begin("x"))
    p(m.end("x"))
    begin; m.offset("y")
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; m.offset(2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; m.begin(2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; m.end(2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    m = /(?<x>q..)?/.match("foobarbaz")
    p(m.offset("x"))
    p(m.begin("x"))
    p(m.end("x"))

    m = /\A\u3042(.)(.)?(.)\z/.match("\u3042\u3043\u3044")
    p(m.offset(1))
    p(m.offset(2))
    p(m.offset(3))