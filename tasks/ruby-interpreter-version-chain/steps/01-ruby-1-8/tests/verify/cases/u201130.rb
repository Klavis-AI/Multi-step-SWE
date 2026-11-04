# encoding: ASCII-8BIT   # make sure this runs in binary mode
def check_both_ways(utf8, raw, encoding)
    assert_equal(utf8.force_encoding('utf-8'), raw.encode('utf-8', encoding),utf8.dump)
    assert_equal(raw.force_encoding(encoding), utf8.encode(encoding, 'utf-8'))
  end
def check_both_ways2(str1, enc1, str2, enc2)
    assert_equal(str1.force_encoding(enc1), str2.encode(enc1, enc2))
    assert_equal(str2.force_encoding(enc2), str1.encode(enc2, enc1))
  end
def check_utf_16_both_ways(utf8, raw)
    copy = raw.dup
    0.step(copy.length-1, 2) { |i| copy[i+1], copy[i] = copy[i], copy[i+1] }
    check_both_ways(utf8, raw, 'utf-16be')
    check_both_ways(utf8, copy, 'utf-16le')
  end
def check_utf_32_both_ways(utf8, raw)
    copy = raw.dup
    0.step(copy.length-1, 4) do |i|
      copy[i+3], copy[i+2], copy[i+1], copy[i] = copy[i], copy[i+1], copy[i+2], copy[i+3]
    end
    check_both_ways(utf8, raw, 'utf-32be')
    #check_both_ways(utf8, copy, 'utf-32le')
  end
begin; 'abc'.encode('foo', 'bar')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'abc'.encode!('foo', 'bar')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'abc'.force_encoding('utf-8').encode('foo')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'abc'.force_encoding('utf-8').encode!('foo')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; "\x80".encode('utf-8','ASCII-8BIT')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; "\x80".encode('utf-8','US-ASCII')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; "\xA5".encode('utf-8','iso-8859-3')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'hello'.freeze.encode!('iso-8859-1')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; '\u3053\u3093\u306b\u3061\u306f'.freeze.encode!('iso-8859-1')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end