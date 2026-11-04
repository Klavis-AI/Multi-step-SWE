begin
  p([1, 1, 2].tally)
rescue Exception => e
  p e.class
end
