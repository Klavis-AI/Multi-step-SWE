begin
  h = {"str": 1, "two words": 2}
  p h.keys
rescue Exception => e
  p e.class
end
