begin
  p((Struct.new(:a, :b).new(1, 2)).members)
rescue Exception => e
  p e.class
end
