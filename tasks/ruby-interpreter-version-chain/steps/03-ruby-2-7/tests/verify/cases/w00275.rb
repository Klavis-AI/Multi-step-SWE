begin
  p((Struct.new(:a, :b).new(1, 2)).inspect)
rescue Exception => e
  p e.class
end
