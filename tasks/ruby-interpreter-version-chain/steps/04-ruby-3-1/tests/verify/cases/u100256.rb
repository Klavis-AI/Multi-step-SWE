def check(re, ss, fs = [])
    re = Regexp.new(re) unless re.is_a?(Regexp)
    ss = [ss] unless ss.is_a?(Array)
    ss.each do |e, s|
      s ||= e
      assert_match(re, s)
      m = re.match(s)
      assert_equal(e, m[0])
    end
    fs = [fs] unless fs.is_a?(Array)
    fs.each {|s| assert_no_match(re, s) }
  end
def failcheck(re)
    assert_raise(RegexpError) { %r"#{ re }" }
  end
@verbose = $VERBOSE
    $VERBOSE = nil
m = /(...)(...)(...)(...)?/.match("foobarbaz")
    p(m[1])
    p(m[1..3])
    p(m[5])