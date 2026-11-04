begin
  p((Struct.new(:a).new(1)).inspect)
rescue Exception => e
  p e.class
end
