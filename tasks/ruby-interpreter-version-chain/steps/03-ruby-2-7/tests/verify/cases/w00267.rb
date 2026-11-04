begin
  p((Struct.new(:a).new(1)).each { })
rescue Exception => e
  p e.class
end
