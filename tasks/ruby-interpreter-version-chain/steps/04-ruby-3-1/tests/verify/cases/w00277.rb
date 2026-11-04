begin
  p((Struct.new(:a, :b).new(1, 2)).each { })
rescue Exception => e
  p e.class
end
