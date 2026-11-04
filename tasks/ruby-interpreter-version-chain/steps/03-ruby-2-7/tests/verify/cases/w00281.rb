begin
  p((Struct.new(:a, :b).new(1, 2)).deconstruct_keys(nil))
rescue Exception => e
  p e.class
end
