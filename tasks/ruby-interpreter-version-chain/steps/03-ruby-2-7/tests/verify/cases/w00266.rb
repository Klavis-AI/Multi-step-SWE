begin
  p((Struct.new(:a).new(1)).to_h)
rescue Exception => e
  p e.class
end
