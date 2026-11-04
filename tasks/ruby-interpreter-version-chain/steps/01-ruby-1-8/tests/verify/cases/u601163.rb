# encoding: ASCII-8BIT   # make sure this runs in binary mode
# frozen_string_literal: false
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
def assert_conversion_both_ways_utf8(utf8, raw, encoding)
    assert_conversion_both_ways(utf8, 'utf-8', raw, encoding)
  end
def assert_conversion_both_ways(str1, enc1, str2, enc2)
    message = str1.dump+str2.dump
    assert_equal(str1.force_encoding(enc1), str2.encode(enc1, enc2), message)
    assert_equal(str2.force_encoding(enc2), str1.encode(enc2, enc1), message)
  end
def assert_undefined_conversion(str, to, from = nil)
    assert_raise(Encoding::UndefinedConversionError) { str.encode(to, from) }
  end
def assert_undefined_in(str, encoding)
    assert_undefined_conversion(str, 'utf-8', encoding)
  end
def assert_invalid_byte_sequence(str, to, from = nil)
    assert_raise(Encoding::InvalidByteSequenceError) { str.encode(to, from) }
  end
def assert_invalid_in(str, encoding)
    assert_invalid_byte_sequence(str, 'utf-8', encoding)
  end
begin; 'abc'.encode('foo', 'bar')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'abc'.encode!('foo', 'bar')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'abc'.force_encoding('utf-8').encode('foo')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 'abc'.force_encoding('utf-8').encode!('foo')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    assert_undefined_in("\x80", 'ASCII-8BIT')
    assert_invalid_in("\x80", 'US-ASCII')
    assert_undefined_in("\xA5", 'iso-8859-3')
    begin; 'hello'.freeze.encode!('iso-8859-1')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; '\u3053\u3093\u306b\u3061\u306f'.freeze.encode!('iso-8859-1')
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end