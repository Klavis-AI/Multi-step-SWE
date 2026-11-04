begin
  p [1, 1, 2].tally({1 => 10})
rescue Exception => e
  p e.class
end
