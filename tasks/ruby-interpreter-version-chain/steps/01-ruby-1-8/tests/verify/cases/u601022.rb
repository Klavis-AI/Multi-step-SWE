# frozen_string_literal: false
s = "\u{221e}\u{2603}"
    begin; open(s.encode("utf-16be"))
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; open(s.encode("utf-16le"))
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; open(s.encode("utf-32be"))
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; open(s.encode("utf-32le"))
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end