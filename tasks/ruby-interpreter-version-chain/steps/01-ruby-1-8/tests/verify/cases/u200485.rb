BDSIZE = 0x4000000000000000.coerce(0)[0].size
def self.bdsize(x)
    ((x + 1) / 8 + BDSIZE) / BDSIZE * BDSIZE
  end
def bdsize(x)
    self.class.bdsize(x)
  end
p(begin
                          0**-1
                        rescue
                          nil
                        end)