begin
  h = {:a => 1, :b => 2}
  p h
  puts h.to_s
  p h.inspect
rescue Exception => e
  p e.class
end
