begin
  p([1, 2, 3].tally)
rescue Exception => e
  p e.class
end
