begin
  p((Struct.new(:a).new(1)).members)
rescue Exception => e
  p e.class
end
