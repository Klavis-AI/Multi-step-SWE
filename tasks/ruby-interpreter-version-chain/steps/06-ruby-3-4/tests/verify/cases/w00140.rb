begin
  p({a: 1, b: 2}.tally)
rescue Exception => e
  p e.class
end
