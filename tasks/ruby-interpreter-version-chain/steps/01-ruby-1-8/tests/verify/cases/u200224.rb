def check_ec(edst, esrc, eres, dst, src, ec, off, len, opts=nil)
    res = ec.primitive_convert(src, dst, off, len, opts)
    assert_equal([edst.dup.force_encoding("ASCII-8BIT"),
                  esrc.dup.force_encoding("ASCII-8BIT"),
                  eres],
                 [dst.dup.force_encoding("ASCII-8BIT"),
                  src.dup.force_encoding("ASCII-8BIT"),
                  res])
  end
def assert_econv(converted, eres, obuf_bytesize, ec, consumed, rest, opts=nil)
    ec = Encoding::Converter.new(*ec) if Array === ec
    i = consumed + rest
    o = ""
    ret = ec.primitive_convert(i, o, 0, obuf_bytesize, opts)
    assert_equal([converted,    eres,       rest],
                 [o,            ret,           i])
  end
def assert_errinfo(e_res, e_enc1, e_enc2, e_error_bytes, e_readagain_bytes, ec)
    assert_equal([e_res, e_enc1, e_enc2,
                  e_error_bytes && e_error_bytes.dup.force_encoding("ASCII-8BIT"),
                  e_readagain_bytes && e_readagain_bytes.dup.force_encoding("ASCII-8BIT")],
                 ec.primitive_errinfo)
  end
begin; Encoding::Converter.new("UTF-8", "UTF-16BE", Encoding::Converter::UNIVERSAL_NEWLINE_DECORATOR)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Encoding::Converter.new("UTF-16BE", "UTF-8", Encoding::Converter::CRLF_NEWLINE_DECORATOR)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Encoding::Converter.new("UTF-16BE", "UTF-8", Encoding::Converter::CR_NEWLINE_DECORATOR)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    begin; Encoding::Converter.new("UTF-16BE", "UTF-8", Encoding::Converter::UNIVERSAL_NEWLINE_DECORATOR)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Encoding::Converter.new("UTF-8", "UTF-16BE", Encoding::Converter::CRLF_NEWLINE_DECORATOR)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; Encoding::Converter.new("UTF-8", "UTF-16BE", Encoding::Converter::CR_NEWLINE_DECORATOR)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end