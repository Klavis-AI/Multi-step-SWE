BDSIZE = 0x4000000000000000.coerce(0)[0].size
def self.bdsize(x)
    ((x + 1) / 8 + BDSIZE) / BDSIZE * BDSIZE
  end
def bdsize(x)
    self.class.bdsize(x)
  end
big_zero = 0x40000000.coerce(0)[0]
    p((-0x40000002)[big_zero])
    p(0x400000001[big_zero])