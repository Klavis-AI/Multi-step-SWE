BDSIZE = 0x4000000000000000.coerce(0)[0].size
def self.bdsize(x)
    ((x + 1) / 8 + BDSIZE) / BDSIZE * BDSIZE
  end
def bdsize(x)
    self.class.bdsize(x)
  end
p("a".ord.chr)
    begin; (-1).chr
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; 0x100.chr
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end