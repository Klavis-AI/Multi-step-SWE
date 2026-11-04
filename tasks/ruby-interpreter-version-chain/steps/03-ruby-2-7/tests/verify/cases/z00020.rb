begin
  S2 = Struct.new(:a, :b)
  p S2.new(1, 2).to_a
  p S2.new(a: 1, b: 2).to_a
rescue Exception => e
  p e.class
end
