begin
  p (1..4).sum
  p [1.5, 2.5].sum
rescue Exception => e
  p e.class
end
