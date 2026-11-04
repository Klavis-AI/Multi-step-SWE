begin
  p((1..4).step(2).to_a)
rescue Exception => e
  p e.class
end
