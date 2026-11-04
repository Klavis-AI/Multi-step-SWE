begin
  p((Struct.new(:a).new(1)).deconstruct_keys(nil))
rescue Exception => e
  p e.class
end
